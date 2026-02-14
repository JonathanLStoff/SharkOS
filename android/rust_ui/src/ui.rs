static mut CURRENT_MENU: usize = 0;
static mut CURRENT_MENU_PAGE: usize = 0; // pagination index for menu columns
static mut SELECTED_DEVICE: Option<String> = None;

pub fn set_menu(idx: usize) {
    unsafe { CURRENT_MENU = idx; }
}

pub fn get_menu() -> usize {
    unsafe { CURRENT_MENU }
}

pub fn set_selected_device(device: String) {
    unsafe { SELECTED_DEVICE = Some(device); }
}

pub fn get_selected_device() -> Option<String> {
    unsafe { SELECTED_DEVICE.clone() }
}

// Pagination helpers for the menu (4 columns visible per page)
pub fn get_menu_page() -> usize {
    unsafe { CURRENT_MENU_PAGE }
}

pub fn set_menu_page(p: usize) {
    unsafe { CURRENT_MENU_PAGE = p; }
}

pub fn next_menu_page(total_pages: usize) {
    unsafe {
        if total_pages == 0 { return; }
        if CURRENT_MENU_PAGE + 1 < total_pages { CURRENT_MENU_PAGE += 1; } else { CURRENT_MENU_PAGE = total_pages - 1; }
    }
}

pub fn prev_menu_page() {
    unsafe { if CURRENT_MENU_PAGE > 0 { CURRENT_MENU_PAGE -= 1; } }
}

pub struct MenuItem {
    pub icon: Option<&'static str>,
    pub label: String,
    pub target_page: usize,
}

pub enum PageContent {
    Menu { title: String, items: Vec<MenuItem> },
    Text { title: String, content: String },
}

pub fn get_page(idx: usize) -> PageContent {
    match idx {
        0 => PageContent::Menu {
            title: "SharkOS Main Menu".to_string(),
            items: vec![
                MenuItem { icon: Some("🔴"), label: "Infrared".into(), target_page: 1 },
                MenuItem { icon: Some("📻"), label: "Sub-GHz".into(), target_page: 2 },
                MenuItem { icon: Some("🔌"), label: "GPIO".into(), target_page: 3 },
                MenuItem { icon: Some("🔧"), label: "NRF Tools".into(), target_page: 4 },
                MenuItem { icon: Some("📶"), label: "WiFi".into(), target_page: 5 },
                MenuItem { icon: Some("🔵"), label: "BLE".into(), target_page: 6 },
                MenuItem { icon: Some("📁"), label: "Files".into(), target_page: 7 },
                MenuItem { icon: Some("📦"), label: "Apps".into(), target_page: 8 },
                MenuItem { icon: Some("⚙️"), label: "Settings".into(), target_page: 9 },
                MenuItem { icon: Some("📳"), label: "NFC".into(), target_page: 10 },
                MenuItem { icon: Some("💀"), label: "Bad USB".into(), target_page: 11 },
                MenuItem { icon: Some("📡"), label: "Cell Signal Scanning".into(), target_page: 12 },
                MenuItem { icon: Some("🕵️"), label: "Android WiFi Snooping".into(), target_page: 13 },
                MenuItem { icon: Some("🕵️"), label: "Bluetooth Snooping".into(), target_page: 14 },
                MenuItem { icon: Some("🔎"), label: "NFC Raw Signal Reader".into(), target_page: 15 },
            ],
        },
        1 => PageContent::Text { title: "Infrared".into(), content: "Placeholder...".into() },
        2 => PageContent::Text { title: "Sub-GHz".into(), content: "Placeholder...".into() },
        3 => PageContent::Text { title: "GPIO".into(), content: "Placeholder...".into() },
        4 => PageContent::Text { title: "NRF Tools".into(), content: "Placeholder...".into() },
        5 => PageContent::Text { title: "WiFi".into(), content: "Placeholder...".into() },
        6 => PageContent::Text { title: "BLE".into(), content: "Placeholder...".into() },
        7 => PageContent::Text { title: "Files".into(), content: "Placeholder...".into() },
        8 => PageContent::Text { title: "Apps".into(), content: "Placeholder...".into() },
        9 => PageContent::Text { title: "Settings".into(), content: "Placeholder...".into() },
        10 => PageContent::Text { title: "NFC".into(), content: "Placeholder...".into() },
        11 => PageContent::Text { title: "Bad USB".into(), content: "Placeholder...".into() },
        12 => PageContent::Text { title: "Cell Signal Scanning".into(), content: "Placeholder...".into() },
        13 => PageContent::Text { title: "Android WiFi Snooping".into(), content: "Placeholder...".into() },
        14 => PageContent::Text { title: "Bluetooth Snooping".into(), content: "Placeholder...".into() },
        15 => PageContent::Text { title: "NFC Raw Signal Reader".into(), content: "Placeholder...".into() },
        _ => PageContent::Text { title: "SharkOS".into(), content: "Placeholder".into() },
    }
}
