use std::path::PathBuf;

#[test]
fn test_cli_version() {
    let version = frame::cli::VERSION;
    assert_eq!(version, "2.0.0");
}

#[test]
fn test_supported_images() {
    assert!(frame::app_state::is_supported_image(std::path::Path::new("test.jpg")));
    assert!(frame::app_state::is_supported_image(std::path::Path::new("test.PNG")));
    assert!(frame::app_state::is_supported_image(std::path::Path::new("test.webp")));
    assert!(frame::app_state::is_supported_image(std::path::Path::new("test.gif")));
    assert!(!frame::app_state::is_supported_image(std::path::Path::new("test.txt")));
    assert!(!frame::app_state::is_supported_image(std::path::Path::new("test.pdf")));
}

#[test]
fn test_app_state_navigation() {
    let mut state = frame::app_state::AppState::default();
    state.images = vec![
        PathBuf::from("a.png"),
        PathBuf::from("b.png"),
        PathBuf::from("c.png"),
    ];
    state.current_index = Some(0);

    assert_eq!(state.display_index(), 1);
    assert_eq!(state.image_count(), 3);

    assert!(state.next());
    assert_eq!(state.current_index, Some(1));

    assert!(state.next());
    assert_eq!(state.current_index, Some(2));

    // At end, next doesn't change
    assert!(!state.next());
    assert_eq!(state.current_index, Some(2));

    // Prev
    assert!(state.prev());
    assert_eq!(state.current_index, Some(1));

    // First / Last
    assert!(state.first());
    assert_eq!(state.current_index, Some(0));
    assert!(state.last());
    assert_eq!(state.current_index, Some(2));
}

#[test]
fn test_app_state_live_directory_ops() {
    let mut state = frame::app_state::AppState::default();
    state.images = vec![
        PathBuf::from("a.png"),
        PathBuf::from("c.png"),
    ];
    state.current_index = Some(1); // pointing at c.png

    // Insert sorted between a and c
    state.insert_sorted(PathBuf::from("b.png"));
    assert_eq!(state.images, vec![
        PathBuf::from("a.png"),
        PathBuf::from("b.png"),
        PathBuf::from("c.png"),
    ]);
    // current_index should shift to follow c.png
    assert_eq!(state.current_index, Some(2));
    assert_eq!(state.current_path(), Some(std::path::Path::new("c.png")));

    // Rename
    state.rename_path(std::path::Path::new("b.png"), PathBuf::from("d.png"));
    assert_eq!(state.images, vec![
        PathBuf::from("a.png"),
        PathBuf::from("c.png"),
        PathBuf::from("d.png"),
    ]);

    // Remove
    assert!(state.remove_path(std::path::Path::new("a.png")));
    assert_eq!(state.images.len(), 2);
    // After removing 'a.png' at index 0, 'c.png' shifted from index 1 to index 0
    assert_eq!(state.current_index, Some(0));
    assert_eq!(state.current_path(), Some(std::path::Path::new("c.png")));
}

#[test]
fn test_format_file_size() {
    use frame::utils::format_file_size;
    assert_eq!(format_file_size(500), "500 bytes");
    assert_eq!(format_file_size(1024), "1.00 KB");
    assert_eq!(format_file_size(1024 * 1024 * 5), "5.00 MB");
}

#[test]
fn test_rotation() {
    use frame::rotate::Rotation;
    let r = Rotation::R0;
    assert_eq!(r.next_cw(), Rotation::R90);
    assert_eq!(r.next_cw().next_cw(), Rotation::R180);
    assert_eq!(r.next_cw().next_cw().next_cw(), Rotation::R270);
    assert_eq!(r.next_cw().next_cw().next_cw().next_cw(), Rotation::R0);
    assert_eq!(r.next_ccw(), Rotation::R270);
}
