// NativeActivity entrypoint for Android using ndk-glue.
// Implements a basic egui UI loop with EGL / GL context setup for rendering.

use ndk_glue::Event;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use log::info;
use khronos_egl as egl;
use egui_glow::Painter;
use egui::Context;
use ndk::native_window::NativeWindow;
use libloading::Library;
use std::sync::Arc;
use std::ptr::NonNull;
use ndk::event::{InputEvent, MotionAction};

use ndk::looper::{ThreadLooper, Poll};
use std::time::{Duration, SystemTime, UNIX_EPOCH};
use std::fs;
use std::io::Write;

mod theme;
// lightweight placeholder UI module (real egui renderer TBD)
mod ui;
// Protocol for BLE communication
mod traffic;

const PERMISSIONS: &[&str] = &[
    "android.permission.BLUETOOTH_SCAN",
    "android.permission.BLUETOOTH_CONNECT",
    "android.permission.BLUETOOTH_ADVERTISE",
    "android.permission.ACCESS_FINE_LOCATION",
    "android.permission.ACCESS_COARSE_LOCATION",
    "android.permission.READ_PHONE_STATE",
    "android.permission.ACCESS_NETWORK_STATE",
    "android.permission.CHANGE_NETWORK_STATE",
    "android.permission.READ_EXTERNAL_STORAGE",
    "android.permission.WRITE_EXTERNAL_STORAGE",
    "android.permission.NFC",
    "android.permission.CAMERA",
    "android.permission.RECORD_AUDIO",
    "android.permission.MODIFY_AUDIO_SETTINGS",
    "android.permission.INTERNET",
    "android.permission.ACCESS_WIFI_STATE",
    "android.permission.CHANGE_WIFI_STATE",
    "android.permission.ACCESS_BACKGROUND_LOCATION",
    "android.permission.REQUEST_IGNORE_BATTERY_OPTIMIZATIONS",
    "android.permission.WAKE_LOCK",
    "android.permission.FOREGROUND_SERVICE",
    "android.permission.RECEIVE_BOOT_COMPLETED",
    "android.permission.SYSTEM_ALERT_WINDOW",
    "android.permission.WRITE_SETTINGS",
    "android.permission.PACKAGE_USAGE_STATS",
    "android.permission.BIND_ACCESSIBILITY_SERVICE",
    "android.permission.BIND_NOTIFICATION_LISTENER_SERVICE",
    "android.permission.CALL_PHONE",
    "android.permission.SEND_SMS",
    "android.permission.RECEIVE_SMS",
    "android.permission.READ_SMS",
    "android.permission.READ_CONTACTS",
    "android.permission.WRITE_CONTACTS",
    "android.permission.READ_CALENDAR",
    "android.permission.WRITE_CALENDAR",
    "android.permission.BODY_SENSORS",
    "android.permission.ACTIVITY_RECOGNITION",
    "android.permission.READ_PHONE_NUMBERS",
    "android.permission.ANSWER_PHONE_CALLS",
    "android.permission.ACCEPT_HANDOVER",
    "android.permission.UWB_RANGING",
    "android.permission.NEARBY_WIFI_DEVICES",
];

static RUNNING: AtomicBool = AtomicBool::new(true);
static LAST_FRAME_MS: AtomicU64 = AtomicU64::new(0);

type EGLInstance = egl::Instance<egl::Dynamic<Library, egl::EGL1_4>>;

struct GlState {
    egl_instance: EGLInstance,
    egl_display: egl::Display,
    egl_surface: egl::Surface,
    egl_context: egl::Context,
    egui_ctx: Context,
    painter: Painter,
    gl: Arc<glow::Context>,
    window: NativeWindow,
}

fn init_gl(window: NativeWindow) -> Option<GlState> {
    unsafe {
        let lib = Library::new("libEGL.so").map_err(|e| log::error!("Failed to load libEGL.so: {}", e)).ok()?;
        let functions = egl::Dynamic::<libloading::Library, egl::EGL1_4>::load_required(lib).map_err(|e| log::error!("Failed to load EGL functions: {}", e)).ok()?;
        let egl_instance = egl::Instance::new(functions);

        let display = egl_instance.get_display(egl::DEFAULT_DISPLAY)?;
        egl_instance.initialize(display).ok()?;

        let config_attribs = [
            egl::RED_SIZE, 8,
            egl::GREEN_SIZE, 8,
            egl::BLUE_SIZE, 8,
            egl::ALPHA_SIZE, 8,
            egl::DEPTH_SIZE, 16,
            egl::NONE,
        ];
        let config = egl_instance.choose_first_config(display, &config_attribs).ok()??;

        let surface = egl_instance.create_window_surface(display, config, window.ptr().as_ptr() as *mut _, None).ok()?;

        let context_attribs = [
            egl::CONTEXT_CLIENT_VERSION, 2,
            egl::NONE,
        ];
        let context = egl_instance.create_context(display, config, None, &context_attribs).ok()?;

        egl_instance.make_current(display, Some(surface), Some(surface), Some(context)).ok()?;

        let gl = glow::Context::from_loader_function(|s| egl_instance.get_proc_address(s).map_or(std::ptr::null(), |p| p as *const _));
        let gl = Arc::new(gl);
        
        let egui_ctx = Context::default();
        let painter = Painter::new(gl.clone(), "", None).map_err(|e| log::error!("Failed to create painter: {}", e)).ok()?;

        Some(GlState {
            egl_instance,
            egl_display: display,
            egl_surface: surface,
            egl_context: context,
            egui_ctx,
            painter,
            gl,
            window,
        })
    }
}

// Capture a lightweight diagnostic dump when the watchdog fires.
// Writes a file to /sdcard/sharkos_watchdog/ and logs a short summary to logcat.
fn capture_watchdog_dump(reason: &str) {
    let now = SystemTime::now();
    let ts = now.duration_since(UNIX_EPOCH).map(|d| d.as_millis()).unwrap_or(0);
    let primary_dir = "/sdcard/sharkos_watchdog";
    let fallback_dir = "/data/local/tmp/sharkos_watchdog";
    let out_dir = if fs::create_dir_all(primary_dir).is_ok() { primary_dir } else if fs::create_dir_all(fallback_dir).is_ok() { fallback_dir } else { "" };

    let mut content = String::new();
    content.push_str(&format!("Watchdog dump: {}\n", reason));
    content.push_str(&format!("timestamp_ms: {}\n", ts));
    let last = LAST_FRAME_MS.load(Ordering::SeqCst);
    content.push_str(&format!("last_frame_ms: {}\n", last));

    // Current thread backtrace
    content.push_str("\n=== current thread backtrace ===\n");
    let bt = std::backtrace::Backtrace::capture();
    content.push_str(&format!("{:?}\n", bt));

    // Read per-thread kernel stacks from /proc/self/task
    content.push_str("\n=== /proc/self/task stacks ===\n");
    if let Ok(entries) = fs::read_dir("/proc/self/task") {
        for e in entries.flatten() {
            if let Ok(tid_str) = e.file_name().into_string() {
                let comm_path = format!("/proc/self/task/{}/comm", tid_str);
                let stack_path = format!("/proc/self/task/{}/stack", tid_str);
                let comm = fs::read_to_string(&comm_path).unwrap_or_else(|_| "<no comm>".into());
                let stack = fs::read_to_string(&stack_path).unwrap_or_else(|_| "<no stack>".into());
                content.push_str(&format!("-- TID {} ({}) --\n", tid_str, comm.trim()));
                content.push_str(&stack);
                content.push_str("\n");
            }
        }
    } else {
        content.push_str("/proc/self/task not readable\n");
    }

    // Attempt to include a short logcat snapshot (best-effort)
    if let Ok(output) = std::process::Command::new("logcat").arg("-d").output() {
        if let Ok(s) = String::from_utf8(output.stdout) {
            content.push_str("\n=== logcat (snapshot) ===\n");
            content.push_str(&s);
        }
    }

    if out_dir.is_empty() {
        log::error!("Watchdog: no writable dump directory available — emitting diagnostic to logcat");
        for chunk in content.as_bytes().chunks(4 * 1024) {
            if let Ok(s) = std::str::from_utf8(chunk) {
                log::error!("WATCHDOG_DUMP: {}", s);
            }
        }
        return;
    }

    let filename = format!("{}/watchdog-{}.log", out_dir, ts);
    match fs::File::create(&filename) {
        Ok(mut f) => {
            let _ = f.write_all(content.as_bytes());
            log::error!("Watchdog: wrote diagnostic dump to {}", filename);
        }
        Err(e) => {
            log::error!("Watchdog: failed to write dump {}: {:?}. Falling back to logcat (dump follows).", filename, e);
            for chunk in content.as_bytes().chunks(4 * 1024) {
                if let Ok(s) = std::str::from_utf8(chunk) {
                    log::error!("WATCHDOG_DUMP: {}", s);
                }
            }
        }
    }
}

#[cfg(target_os = "android")]
fn setup_logging() {
    android_logger::init_once(android_logger::Config::default().with_max_level(log::LevelFilter::Info));
}

#[cfg(not(target_os = "android"))]
fn setup_logging() {}

#[ndk_glue::main(backtrace = "on")]
fn main() {
    setup_logging();
    
    std::panic::set_hook(Box::new(|info| {
        log::error!("PANIC: {:?}", info);
    }));

    info!("sharkos_gui: main started");

    let mut gl_state: Option<GlState> = None;
    let mut collected_events = Vec::new();

    info!("sharkos_gui: Entering event loop");

// Watchdog thread: logs if the UI hasn't rendered for >2s. This helps diagnose ANRs.
std::thread::spawn(|| {
    loop {
        std::thread::sleep(Duration::from_secs(2));
        let last = LAST_FRAME_MS.load(Ordering::SeqCst);
        let now_ms = SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_millis() as u64;
        if last == 0 || now_ms.saturating_sub(last) > 2000 {
            log::warn!("Watchdog: last draw_frame was {} ms ago", now_ms.saturating_sub(last));
            // best-effort capture diagnostic dump (non-blocking for UI)
            capture_watchdog_dump("watchdog-stall");
        } else {
            log::debug!("Watchdog: main alive (last draw {} ms ago)", now_ms.saturating_sub(last));
        }
    }
});
    
    let looper = ThreadLooper::for_thread().unwrap();
    let mut last_frame_time = std::time::Instant::now();

    loop {
        if !RUNNING.load(Ordering::SeqCst) {
             break;
        }

        let poll_result = looper.poll_once_timeout(Duration::from_millis(10));
        match poll_result {
            Ok(Poll::Wake) => {},
            Ok(Poll::Callback) => {},
            Ok(Poll::Timeout) => {},
            Ok(Poll::Event { ident, .. }) => {}, 
            Err(_) => {}
        }

        // Handle Android Input Events (timed)
        let input_drain_start = std::time::Instant::now();
        if let Some(queue) = ndk_glue::input_queue().as_ref() {
            while let Ok(Some(event)) = queue.get_event() {
                if let Some(event) = queue.pre_dispatch(event) {
                    match &event {
                        InputEvent::MotionEvent(motion) => {
                            let phase = match motion.action() {
                                MotionAction::Down | MotionAction::PointerDown => Some(egui::TouchPhase::Start),
                                MotionAction::Move => Some(egui::TouchPhase::Move),
                                MotionAction::Up | MotionAction::PointerUp => Some(egui::TouchPhase::End),
                                MotionAction::Cancel => Some(egui::TouchPhase::Cancel),
                                _ => None,
                            };
                            
                            if let Some(phase) = phase {
                                for ptr in motion.pointers() {
                                    let pid = ptr.pointer_id();
                                    let x = ptr.x();
                                    let y = ptr.y();
                                    
                                    collected_events.push(egui::Event::Touch {
                                        device_id: egui::TouchDeviceId(0),
                                        id: egui::TouchId::from(pid as u64),
                                        phase,
                                        pos: egui::pos2(x, y),
                                        force: None, 
                                    });
                                }
                            }
                        }
                        _ => {}
                    }
                    queue.finish_event(event, false);
                }
            }
        }
        let input_drain_dur = input_drain_start.elapsed();
        if input_drain_dur > Duration::from_millis(50) {
            log::warn!("Input drain took {:?}", input_drain_dur);
        }

        while let Some(ev) = ndk_glue::poll_events() {
             match ev {
                Event::WindowCreated => {
                    info!("Window created");
                    if let Some(window) = ndk_glue::native_window() {
                        unsafe {
                            let ptr = window.ptr();
                            let ptr = NonNull::new(ptr.as_ptr()).unwrap();
                            let native_window = ndk::native_window::NativeWindow::from_ptr(ptr);
                            gl_state = init_gl(native_window);
                        }
                        
                         if let Some(ref mut state) = gl_state {
                             info!("GL initialized successfully, drawing first frame");
                             theme::apply_theme(&state.egui_ctx);
                             state.egui_ctx.set_pixels_per_point(3.0); 
                             state.egui_ctx.request_repaint(); 
                         } else {
                             info!("Failed to initialize GL");
                         }
                    }
                }
                Event::WindowResized | Event::WindowRedrawNeeded | Event::ContentRectChanged | Event::WindowHasFocus => {
                    if let Some(ref mut state) = gl_state {
                        let frame_start = std::time::Instant::now();
                        draw_frame(state, &mut collected_events);
                        let frame_dur = frame_start.elapsed();
                        if frame_dur > Duration::from_millis(100) {
                            log::warn!("draw_frame (resize) took {:?}", frame_dur);
                        }
                    }
                }
                Event::WindowDestroyed => {
                    info!("Window destroyed");
                    gl_state = None;
                }
                Event::Destroy => {
                    info!("Received Destroy event");
                    RUNNING.store(false, Ordering::SeqCst);
                }
                _ => {}
             }
        }

        if let Some(ref mut state) = gl_state {
            let now = std::time::Instant::now();
            if !collected_events.is_empty() || now.duration_since(last_frame_time) > Duration::from_millis(16) {
                last_frame_time = now;
                let frame_start = std::time::Instant::now();
                draw_frame(state, &mut collected_events);
                let frame_dur = frame_start.elapsed();
                if frame_dur > Duration::from_millis(100) {
                    log::warn!("draw_frame loop took {:?}", frame_dur);
                }
            }
        }
    }
}

fn draw_frame(state: &mut GlState, events: &mut Vec<egui::Event>) {
    unsafe {
        use glow::HasContext as _;
        let width = state.window.width();
        let height = state.window.height();
        
        state.gl.viewport(0, 0, width as i32, height as i32);
        state.gl.clear_color(0.0, 0.0, 0.0, 1.0); 
        state.gl.clear(glow::COLOR_BUFFER_BIT);

        let mut raw_input = egui::RawInput::default();
        raw_input.screen_rect = Some(egui::Rect::from_min_size(
            egui::Pos2::ZERO,
            egui::vec2(width as f32, height as f32),
        ));
        
        // Move events into raw_input
        raw_input.events.append(events);

        let full_output = state.egui_ctx.run(raw_input, |ctx| {
                egui::CentralPanel::default().show(ctx, |ui| {
                    match ui::get_page(ui::get_menu()) {
                        ui::PageContent::Menu { title, items } => {
                            ui.heading(title);
                            ui.add_space(10.0);

                            let avail = ui.available_size();
                            let visible_width = avail.x;
                            let visible_height = avail.y;

                            // Reserve a small header area then split remaining height into two rows
                            let reserved_v = 20.0; // spacing and headings
                            let item_height = ((visible_height - reserved_v).max(1.0)) / 2.0;

                            // Make 4 columns visible in the viewport (flex behavior like CSS flex-basis: 25%)
                            let item_width = (visible_width / 4.0).max(1.0);

                            // Tight, clamped button sizing so we never overflow vertically or horizontally
                            ui.spacing_mut().button_padding = egui::vec2(6.0, 6.0);
                            let mut btn_width = item_width * 0.85;
                            btn_width = btn_width.clamp(56.0, item_width * 0.95);
                            let mut btn_height = (item_height * 0.45).max(36.0);
                            btn_height = btn_height.min(item_height * 0.9);

                            let columns = ((items.len() + 1) / 2) as usize; // two rows per column
                            let total_width = item_width * columns as f32;
                            let pages = (columns + 3) / 4; // 4 columns per page
                            let current_page = ui::get_menu_page().min(pages.saturating_sub(1));

                            // Pagination controls (snap-to-page behavior)
                            if pages > 1 {
                                ui.horizontal(|ui| {
                                    if ui.add(egui::Button::new("◀")).clicked() {
                                        ui::prev_menu_page();
                                    }
                                    ui.label(format!("Page {}/{}", current_page + 1, pages));
                                    if ui.add(egui::Button::new("▶")).clicked() {
                                        ui::next_menu_page(pages);
                                    }
                                });
                            }

                            // horizontal-only scroll area, fixed max height to avoid vertical scroll
                            egui::ScrollArea::horizontal()
                                .auto_shrink([false, false])
                                .max_height(item_height * 2.0 + reserved_v)
                                .show(ui, |ui| {
                                    ui.set_min_width(total_width);
                                    ui.set_min_height(item_height * 2.0 + reserved_v);

                                    ui.horizontal(|ui| {
                                        let start_col = current_page * 4;
                                        let end_col = (start_col + 4).min(columns);

                                        for col in start_col..end_col {
                                            ui.vertical(|ui| {
                                                let top_idx = col * 2;
                                                if top_idx < items.len() {
                                                    let item = &items[top_idx];
                                                    let icon = item.icon.unwrap_or("");
                                                    let label = format!("{}\n{}", icon, &item.label);
                                                    let btn = egui::Button::new(egui::RichText::new(label).size(14.0));
                                                    if ui.add_sized([btn_width, btn_height], btn).clicked() {
                                                        ui::set_menu(item.target_page);
                                                    }
                                                }

                                                let bot_idx = top_idx + 1;
                                                if bot_idx < items.len() {
                                                    let item = &items[bot_idx];
                                                    let icon = item.icon.unwrap_or("");
                                                    let label = format!("{}\n{}", icon, &item.label);
                                                    let btn = egui::Button::new(egui::RichText::new(label).size(14.0));
                                                    if ui.add_sized([btn_width, btn_height], btn).clicked() {
                                                        ui::set_menu(item.target_page);
                                                    }
                                                }
                                            });
                                        }
                                    });
                                });
                        }
                        ui::PageContent::Text { title, content } => {
                            ui.heading(title);
                            ui.add_space(20.0);
                            ui.label(content);
                            ui.add_space(20.0);
                            if ui.button("Back").clicked() {
                                ui::set_menu(0);
                            }
                        }
                    }
                });
        });

        let shapes = state.egui_ctx.tessellate(full_output.shapes, full_output.pixels_per_point);
        
        state.painter.paint_and_update_textures(
                [width as u32, height as u32],
                full_output.pixels_per_point,
                &shapes,
                &full_output.textures_delta,
            );

        if let Err(e) = state.egl_instance.swap_buffers(state.egl_display, state.egl_surface) {
            log::error!("Swap buffers failed: {:?}", e);
        }

        // update watchdog timestamp for last successful frame
        LAST_FRAME_MS.store(SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_millis() as u64, Ordering::SeqCst);
    }
}
