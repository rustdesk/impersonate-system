use auto_elevate;

// build && run as administrator

fn main() {
    auto_elevate::run_as_system("C:\\Windows\\System32\\notepad.exe", " hello.txt").ok();
    // auto_elevate::run_as_system(
    //     "D:\\rustdesk\\rustdesk\\rustdesk\\rustdesk-1.2.0-setdown.exe",
    //     "--noinstall",
    // )
    // .ok();
}