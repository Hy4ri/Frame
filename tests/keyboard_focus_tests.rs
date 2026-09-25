use frame::frame_app::FrameApp;
use gpui::{AppContext, TestAppContext};
use gpui_kit::component::Root;
use std::fs::File;
use std::io::Write;

#[gpui::test]
fn test_keyboard_navigation_startup(cx: &mut TestAppContext) {
    unsafe {
        std::env::set_var("FRAME_TEST", "1");
    }
    cx.update(|cx| {
        gpui_kit::init(cx);
        frame::keybindings::register(cx);
    });

    let tmp = tempfile::tempdir().unwrap();
    let img1 = tmp.path().join("1.png");
    let img2 = tmp.path().join("2.png");
    let img3 = tmp.path().join("3.png");

    let mut f1 = File::create(&img1).unwrap();
    f1.write_all(&[0x89, b'P', b'N', b'G']).unwrap();
    let mut f2 = File::create(&img2).unwrap();
    f2.write_all(&[0x89, b'P', b'N', b'G']).unwrap();
    let mut f3 = File::create(&img3).unwrap();
    f3.write_all(&[0x89, b'P', b'N', b'G']).unwrap();

    let initial = Some(img1.clone());

    let mut app_entity = None;
    let (_root, cx) = cx.add_window_view(|window, cx| {
        let view = cx.new(|cx| FrameApp::new(initial, window, cx));
        app_entity = Some(view.clone());
        Root::new(view, window, cx)
    });

    let app = app_entity.expect("app view exists");

    let idx_before = cx.update(|_window, cx| app.read(cx).app_state.current_index);
    assert_eq!(idx_before, Some(0));

    // Simulate pressing right arrow directly on the window
    cx.simulate_keystrokes("right");

    let idx_after = cx.update(|_window, cx| app.read(cx).app_state.current_index);
    assert_eq!(
        idx_after,
        Some(1),
        "Shortcuts should advance current image on startup without click"
    );

    // Test gg jumps to first
    cx.simulate_keystrokes("g g");
    let idx_first = cx.update(|_window, cx| app.read(cx).app_state.current_index);
    assert_eq!(idx_first, Some(0));

    // Test G jumps to last
    cx.simulate_keystrokes("shift-g");
    let idx_last = cx.update(|_window, cx| app.read(cx).app_state.current_index);
    assert_eq!(idx_last, Some(2));
}
