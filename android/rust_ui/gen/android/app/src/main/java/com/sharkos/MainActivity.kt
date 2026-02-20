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

class MainActivity : TauriActivity() {
  // Enable webview back-navigation handling so Android back button navigates menus
  override val handleBackNavigation: Boolean = true

  private var webViewRef: WebView? = null

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

  // Synchronous helper exposed to JNI: connect to device by MAC, discover
  // services and write the provided payload to the command characteristic.
  // Returns true on success, false otherwise.
  @SuppressLint("MissingPermission")
  fun gattWriteCommand(mac: String, payload: String): Boolean {
    try {
      val adapter = android.bluetooth.BluetoothAdapter.getDefaultAdapter() ?: return false
      if (ActivityCompat.checkSelfPermission(this, android.Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED) {
        // Missing runtime permission
        return false
      }

      val device = adapter.getRemoteDevice(mac)
      val latch = java.util.concurrent.CountDownLatch(1)
      val writeOk = java.util.concurrent.atomic.AtomicBoolean(false)

      val svcUuid = java.util.UUID.fromString("4fafc201-1fbd-459e-8fcc-c5c9c331914b")
      val cmdCharUuid = java.util.UUID.fromString("2a73f2c0-1fbd-459e-8fcc-c5c9c331914c")

      var gattRef: android.bluetooth.BluetoothGatt? = null

      val cb = object : android.bluetooth.BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: android.bluetooth.BluetoothGatt, status: Int, newState: Int) {
          if (newState == android.bluetooth.BluetoothProfile.STATE_CONNECTED) {
            gatt.discoverServices()
          } else {
            try { gatt.close() } catch (_: Exception) {}
            latch.countDown()
          }
        }

        override fun onServicesDiscovered(gatt: android.bluetooth.BluetoothGatt, status: Int) {
          val svc = gatt.getService(svcUuid)
          if (svc == null) {
            gatt.disconnect()
            gatt.close()
            latch.countDown()
            return
          }
          val ch = svc.getCharacteristic(cmdCharUuid)
          if (ch == null) {
            gatt.disconnect()
            gatt.close()
            latch.countDown()
            return
          }
          ch.value = payload.toByteArray(Charsets.UTF_8)
          val writeStarted = gatt.writeCharacteristic(ch)
          if (!writeStarted) {
            gatt.disconnect()
            gatt.close()
            latch.countDown()
          }
        }

        override fun onCharacteristicWrite(gatt: android.bluetooth.BluetoothGatt, characteristic: android.bluetooth.BluetoothGattCharacteristic, status: Int) {
          writeOk.set(status == android.bluetooth.BluetoothGatt.GATT_SUCCESS)
          try { gatt.disconnect(); gatt.close() } catch (_: Exception) {}
          latch.countDown()
        }
      }

      // Connect (runs async; callback will signal latch)
      gattRef = device.connectGatt(this, false, cb)

      // Wait up to 5 seconds for the write to complete
      val completed = latch.await(5, java.util.concurrent.TimeUnit.SECONDS)
      if (!completed) {
        try { gattRef?.disconnect(); gattRef?.close() } catch (_: Exception) {}
        return false
      }
      return writeOk.get()
    } catch (ex: Exception) {
      ex.printStackTrace()
      return false
    }
  }

  companion object {
    const val PERMISSION_REQUEST_CODE = 4242
  }
}

