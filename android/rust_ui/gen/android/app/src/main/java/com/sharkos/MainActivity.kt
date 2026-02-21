package com.sharkos

import android.os.Bundle
import androidx.activity.enableEdgeToEdge
import android.webkit.WebView
import android.webkit.JavascriptInterface
import androidx.core.app.ActivityCompat
import android.content.pm.PackageManager
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import org.json.JSONArray
import org.json.JSONObject
import android.view.View
import android.annotation.SuppressLint
import java.util.UUID

class MainActivity : TauriActivity() {
  // Enable webview back-navigation handling so Android back button navigates menus
  override val handleBackNavigation: Boolean = true

  private var webViewRef: WebView? = null
  // BLE GATT persistent state
  private var currentGatt: android.bluetooth.BluetoothGatt? = null
  private var gattCallback: android.bluetooth.BluetoothGattCallback? = null
  @Volatile private var isGattConnected = false
  @Volatile private var notificationsEnabled = false
  // Per-write synchronization (only one outstanding write at a time from JNI)
  @Volatile private var pendingPayload: String? = null
  private var writeLatch: java.util.concurrent.CountDownLatch? = null
  private var writeOk: java.util.concurrent.atomic.AtomicBoolean? = null
  // BLE UUIDs (class-level to avoid re-parsing)
  private val svcUuid: java.util.UUID = java.util.UUID.fromString("4fafc201-1fbd-459e-8fcc-c5c9c331914b")
  private val cmdCharUuid: java.util.UUID = java.util.UUID.fromString("2a73f2c0-1fbd-459e-8fcc-c5c9c331914c")
  private val statusCharUuid: java.util.UUID = java.util.UUID.fromString("3a2743a1-1fbd-459e-8fcc-c5c9c331914d")
  private val cccdUuid: java.util.UUID = java.util.UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

  /**
   * Helper invoked from BLE callback when a notification arrives. Emits a
   * `radio-signal` event to the frontend, quoting the payload appropriately.
   */
  private fun handleRadioNotification(bytes: ByteArray) {
    android.util.Log.i("sharkos_lib::MainActivity", "[handleRadioNotification] got ${bytes.size} bytes")
    // try treat as UTF-8 JSON if it looks like an object
    val possible = try { String(bytes, Charsets.UTF_8) } catch (_: Exception) { null }
    val payload = if (possible != null && possible.trimStart().startsWith("{")) {
      // JSON string, send raw
      JSONObject.quote(possible)
    } else {
      val b64 = android.util.Base64.encodeToString(bytes, android.util.Base64.NO_WRAP)
      JSONObject.quote("PROTO:$b64")
    }
    webViewRef?.post {
      webViewRef?.evaluateJavascript("window.dispatchEvent(new CustomEvent('radio-signal', { detail: $payload }));", null)
    }
  }

  override fun onCreate(savedInstanceState: Bundle?) {
    enableEdgeToEdge()
    super.onCreate(savedInstanceState)
    applyImmersiveMode()
  }

  override fun onWebViewCreate(webView: WebView) {
    super.onWebViewCreate(webView)
    webViewRef = webView

    // Add a small JS bridge for requesting runtime permissions from the frontend
    webView.addJavascriptInterface(object {
      @JavascriptInterface
      fun hasPermissions(permissionsJson: String): Boolean {
        return try {
          val arr = JSONArray(permissionsJson)
          val perms = Array(arr.length()) { i -> arr.getString(i) }
          PermissionHelper.hasPermissions(this@MainActivity, perms)
        } catch (e: Exception) {
          false
        }
      }

      @JavascriptInterface
      fun requestPermissions(permissionsJson: String) {
        try {
          val arr = JSONArray(permissionsJson)
          val perms = Array(arr.length()) { i -> arr.getString(i) }
          val toRequest = perms.filter { p -> !PermissionHelper.hasPermissions(this@MainActivity, arrayOf(p)) }
          if (toRequest.isNotEmpty()) {
            // requestPermissions MUST run on the UI thread or Android silently ignores it
            this@MainActivity.runOnUiThread {
              ActivityCompat.requestPermissions(this@MainActivity, toRequest.toTypedArray(), PERMISSION_REQUEST_CODE)
            }
          } else {
            val res = JSONObject()
            res.put("granted", true)
            webViewRef?.post { webViewRef?.evaluateJavascript("window.dispatchEvent(new CustomEvent('permissions-result', { detail: ${res.toString()} }));", null) }
          }
        } catch (e: Exception) {
          val res = JSONObject()
          res.put("error", e.message)
          webViewRef?.post { webViewRef?.evaluateJavascript("window.dispatchEvent(new CustomEvent('permissions-result', { detail: ${res.toString()} }));", null) }
        }
      }
    }, "AndroidBridge")

    webView.post { applyImmersiveMode() }
  }

  override fun onWindowFocusChanged(hasFocus: Boolean) {
    super.onWindowFocusChanged(hasFocus)
    if (hasFocus) {
      applyImmersiveMode()
    }
  }

  @Deprecated("Deprecated in Java")
  override fun onBackPressed() {
    val webView = webViewRef
    if (webView != null && webView.canGoBack()) {
      webView.goBack()
      return
    }
    super.onBackPressed()
  }

  private fun applyImmersiveMode() {
    WindowCompat.setDecorFitsSystemWindows(window, false)
    val controller = WindowInsetsControllerCompat(window, window.decorView)
    controller.hide(WindowInsetsCompat.Type.statusBars() or WindowInsetsCompat.Type.navigationBars())
    controller.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
  }

  override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<String>, grantResults: IntArray) {
    super.onRequestPermissionsResult(requestCode, permissions, grantResults)
    val obj = JSONObject()
    val granted = JSONObject()
    for (i in permissions.indices) {
      granted.put(permissions[i], grantResults[i] == PackageManager.PERMISSION_GRANTED)
    }
    obj.put("granted", granted)
    webViewRef?.post { webViewRef?.evaluateJavascript("window.dispatchEvent(new CustomEvent('permissions-result', { detail: ${obj.toString()} }));", null) }
  }

  // ---------- BLE GATT helpers ----------

  @SuppressLint("MissingPermission")
  private fun writePendingCommand(gatt: android.bluetooth.BluetoothGatt) {
    val payload = pendingPayload
    if (payload == null) {
      android.util.Log.i("sharkos_lib::MainActivity", "writePendingCommand: no pending payload")
      writeLatch?.countDown()
      return
    }
    val svc = gatt.getService(svcUuid)
    if (svc == null) {
      android.util.Log.i("sharkos_lib::MainActivity", "writePendingCommand: service not found")
      writeLatch?.countDown()
      return
    }
    val ch = svc.getCharacteristic(cmdCharUuid)
    if (ch == null) {
      android.util.Log.i("sharkos_lib::MainActivity", "writePendingCommand: cmd char not found")
      writeLatch?.countDown()
      return
    }
    ch.value = payload.toByteArray(Charsets.UTF_8)
    val ok = gatt.writeCharacteristic(ch)
    android.util.Log.i("sharkos_lib::MainActivity", "writePendingCommand: writeCharacteristic('$payload') = $ok")
    if (!ok) writeLatch?.countDown()
  }

  @SuppressLint("MissingPermission")
  private fun enableNotificationsOnStatusChar(gatt: android.bluetooth.BluetoothGatt): Boolean {
    val svc = gatt.getService(svcUuid)
    if (svc == null) {
      android.util.Log.i("sharkos_lib::MainActivity", "enableNotifications: service not found")
      return false
    }
    val statusCh = svc.getCharacteristic(statusCharUuid)
    if (statusCh == null) {
      android.util.Log.i("sharkos_lib::MainActivity", "enableNotifications: status char not found")
      return false
    }
    gatt.setCharacteristicNotification(statusCh, true)
    val desc = statusCh.getDescriptor(cccdUuid)
    if (desc == null) {
      android.util.Log.i("sharkos_lib::MainActivity", "enableNotifications: CCCD descriptor not found")
      return false
    }
    desc.value = android.bluetooth.BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
    val ok = gatt.writeDescriptor(desc)
    android.util.Log.i("sharkos_lib::MainActivity", "enableNotifications: writeDescriptor = $ok")
    return ok
  }

  @SuppressLint("MissingPermission")
  private fun ensureGattCallback(): android.bluetooth.BluetoothGattCallback {
    gattCallback?.let { return it }
    val cb = object : android.bluetooth.BluetoothGattCallback() {
      override fun onConnectionStateChange(gatt: android.bluetooth.BluetoothGatt, status: Int, newState: Int) {
        android.util.Log.i("sharkos_lib::MainActivity", "onConnectionStateChange status=$status newState=$newState")
        if (newState == android.bluetooth.BluetoothProfile.STATE_CONNECTED) {
          isGattConnected = true
          // Request a larger MTU so BLE notifications > 20 bytes are not truncated.
          // The ESP32 typically supports 512; Android will negotiate the actual value.
          android.util.Log.i("sharkos_lib::MainActivity", "GATT connected – requesting MTU 512")
          gatt.requestMtu(512)
        } else if (newState == android.bluetooth.BluetoothProfile.STATE_DISCONNECTED) {
          isGattConnected = false
          notificationsEnabled = false
          android.util.Log.i("sharkos_lib::MainActivity", "GATT disconnected (status=$status)")
          writeLatch?.countDown()
        }
      }

      override fun onServicesDiscovered(gatt: android.bluetooth.BluetoothGatt, status: Int) {
        android.util.Log.i("sharkos_lib::MainActivity", "onServicesDiscovered status=$status")
        if (status != android.bluetooth.BluetoothGatt.GATT_SUCCESS) {
          android.util.Log.i("sharkos_lib::MainActivity", "service discovery failed")
          writeLatch?.countDown()
          return
        }
        if (!notificationsEnabled) {
          // Enable notifications first; the actual command write happens in
          // onDescriptorWrite after the CCCD write completes.
          val ok = enableNotificationsOnStatusChar(gatt)
          if (!ok) {
            // Could not start notification subscription – write command anyway
            writePendingCommand(gatt)
          }
        } else {
          writePendingCommand(gatt)
        }
      }

      override fun onMtuChanged(gatt: android.bluetooth.BluetoothGatt, mtu: Int, status: Int) {
        android.util.Log.i("sharkos_lib::MainActivity", "onMtuChanged mtu=$mtu status=$status")
        // MTU negotiated, now discover services
        gatt.discoverServices()
      }

      override fun onDescriptorWrite(gatt: android.bluetooth.BluetoothGatt, descriptor: android.bluetooth.BluetoothGattDescriptor, status: Int) {
        android.util.Log.i("sharkos_lib::MainActivity", "onDescriptorWrite status=$status")
        notificationsEnabled = (status == android.bluetooth.BluetoothGatt.GATT_SUCCESS)
        // Now safe to write the actual command (only one GATT op at a time)
        writePendingCommand(gatt)
      }

      override fun onCharacteristicWrite(gatt: android.bluetooth.BluetoothGatt, characteristic: android.bluetooth.BluetoothGattCharacteristic, status: Int) {
        android.util.Log.i("sharkos_lib::MainActivity", "onCharacteristicWrite status=$status")
        writeOk?.set(status == android.bluetooth.BluetoothGatt.GATT_SUCCESS)
        val payload = pendingPayload
        if (payload != null && payload.contains(".stop")) {
          android.util.Log.i("sharkos_lib::MainActivity", "stop command – disconnecting")
          try { gatt.disconnect() } catch (_: Exception) {}
          isGattConnected = false
        }
        writeLatch?.countDown()
      }

      override fun onCharacteristicChanged(gatt: android.bluetooth.BluetoothGatt, characteristic: android.bluetooth.BluetoothGattCharacteristic) {
        val bytes = characteristic.value
        android.util.Log.i("sharkos_lib::MainActivity", "[onCharacteristicChanged] ${bytes?.size ?: 0} bytes")
        if (bytes != null && bytes.isNotEmpty()) handleRadioNotification(bytes)
      }
    }
    gattCallback = cb
    return cb
  }

  // Synchronous helper exposed to JNI: connect to device by MAC, discover
  // services, enable notifications, and write the provided payload to the
  // command characteristic.  Maintains a persistent GATT connection so
  // subsequent writes re-use it and notifications keep flowing.
  @SuppressLint("MissingPermission")
  fun gattWriteCommand(mac: String, payload: String): Boolean {
    try {
      android.util.Log.i("sharkos_lib::MainActivity",
        "gattWriteCommand: mac=$mac payload='$payload' connected=$isGattConnected")

      val adapter = android.bluetooth.BluetoothAdapter.getDefaultAdapter()
      if (adapter == null) {
        android.util.Log.i("sharkos_lib::MainActivity", "gattWriteCommand: no adapter")
        return false
      }
      if (ActivityCompat.checkSelfPermission(this,
            android.Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED) {
        android.util.Log.i("sharkos_lib::MainActivity", "gattWriteCommand: missing BLUETOOTH_CONNECT")
        return false
      }

      // Per-write synchronization state
      pendingPayload = payload
      val latch = java.util.concurrent.CountDownLatch(1)
      writeLatch = latch
      val result = java.util.concurrent.atomic.AtomicBoolean(false)
      writeOk = result

      val cb = ensureGattCallback()

      if (isGattConnected && currentGatt != null) {
        // ---- fast path: reuse existing connection ----
        android.util.Log.i("sharkos_lib::MainActivity", "gattWriteCommand: reusing existing connection")
        val gatt = currentGatt!!
        val svc = gatt.getService(svcUuid)
        if (svc != null && notificationsEnabled) {
          writePendingCommand(gatt)
        } else if (svc != null) {
          val ok = enableNotificationsOnStatusChar(gatt)
          if (!ok) writePendingCommand(gatt)
        } else {
          gatt.discoverServices()
        }
      } else {
        // ---- slow path: establish new BLE connection ----
        val device = adapter.getRemoteDevice(mac)
        this.runOnUiThread {
          if (currentGatt != null) {
            android.util.Log.i("sharkos_lib::MainActivity", "gattWriteCommand: closing stale GATT")
            try { currentGatt?.disconnect(); currentGatt?.close() } catch (_: Exception) {}
            currentGatt = null
            isGattConnected = false
            notificationsEnabled = false
          }
          try { adapter.cancelDiscovery() } catch (_: Exception) {}

          android.util.Log.i("sharkos_lib::MainActivity",
            "gattWriteCommand: connectGatt to $mac (TRANSPORT_LE, PHY_LE_1M, mainHandler)")
          currentGatt = try {
            val handler = android.os.Handler(android.os.Looper.getMainLooper())
            device.connectGatt(this, false, cb,
              android.bluetooth.BluetoothDevice.TRANSPORT_LE,
              android.bluetooth.BluetoothDevice.PHY_LE_1M, handler)
          } catch (e: Exception) {
            android.util.Log.e("sharkos_lib::MainActivity", "connectGatt threw: ${e.message}")
            writeLatch?.countDown()
            null
          }
          android.util.Log.i("sharkos_lib::MainActivity",
            "gattWriteCommand: connectGatt returned $currentGatt")
        }
      }

      // Wait for the write to complete (or timeout)
      val completed = latch.await(30, java.util.concurrent.TimeUnit.SECONDS)
      if (!completed) {
        android.util.Log.i("sharkos_lib::MainActivity", "gattWriteCommand: timed out (30s)")
        // Don't close – connection may still be establishing
        return false
      }
      return result.get()
    } catch (ex: Exception) {
      android.util.Log.e("sharkos_lib::MainActivity", "gattWriteCommand exception: ${ex.message}")
      ex.printStackTrace()
      return false
    }
  }

  companion object {
    const val PERMISSION_REQUEST_CODE = 4242
  }
}

