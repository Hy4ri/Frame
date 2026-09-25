use std::path::PathBuf;

#[test]
fn test_cli_version() {
    let version = frame::cli::VERSION;
    assert_eq!(version, env!("CARGO_PKG_VERSION"));
}

#[test]
fn test_supported_images() {
    assert!(frame::app_state::is_supported_image(std::path::Path::new(
        "test.jpg"
    )));
    assert!(frame::app_state::is_supported_image(std::path::Path::new(
        "test.PNG"
    )));
    assert!(frame::app_state::is_supported_image(std::path::Path::new(
        "test.webp"
    )));
    assert!(frame::app_state::is_supported_image(std::path::Path::new(
        "test.gif"
    )));
    assert!(frame::app_state::is_supported_image(std::path::Path::new(
        "test.apng"
    )));
    assert!(!frame::app_state::is_supported_image(std::path::Path::new(
        "test.txt"
    )));
    assert!(!frame::app_state::is_supported_image(std::path::Path::new(
        "test.pdf"
    )));
}

#[test]
fn test_natural_sort() {
    let mut names = vec!["img10.png", "img2.png", "img1.png", "IMG20.png"];
    names.sort_by(|a, b| frame::app_state::natural_cmp(a, b));
    assert_eq!(
        names,
        vec!["img1.png", "img2.png", "img10.png", "IMG20.png"]
    );
}

#[test]
#[allow(clippy::field_reassign_with_default)]
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
#[allow(clippy::field_reassign_with_default)]
fn test_app_state_live_directory_ops() {
    let mut state = frame::app_state::AppState::default();
    state.images = vec![PathBuf::from("a.png"), PathBuf::from("c.png")];
    state.current_index = Some(1); // pointing at c.png

    // Insert sorted between a and c
    state.insert_sorted(PathBuf::from("b.png"));
    assert_eq!(
        state.images,
        vec![
            PathBuf::from("a.png"),
            PathBuf::from("b.png"),
            PathBuf::from("c.png"),
        ]
    );
    // current_index should shift to follow c.png
    assert_eq!(state.current_index, Some(2));
    assert_eq!(state.current_path(), Some(std::path::Path::new("c.png")));

    // Rename
    state.rename_path(std::path::Path::new("b.png"), PathBuf::from("d.png"));
    assert_eq!(
        state.images,
        vec![
            PathBuf::from("a.png"),
            PathBuf::from("c.png"),
            PathBuf::from("d.png"),
        ]
    );

    // Rename to unsupported format removes it
    state.rename_path(std::path::Path::new("d.png"), PathBuf::from("d.txt"));
    assert_eq!(
        state.images,
        vec![PathBuf::from("a.png"), PathBuf::from("c.png")]
    );

    // Remove
    assert!(state.remove_path(std::path::Path::new("a.png")));
    assert_eq!(state.images.len(), 1);
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

#[test]
fn test_cache_contains_and_lru() {
    use frame::cache::ImageCache;
    use gpui_kit::RenderImage;
    use image::Frame;
    use smallvec::SmallVec;
    use std::sync::Arc;

    let cache = ImageCache::new(2, 1024 * 1024);
    let dummy_img = || {
        let frame = Frame::new(image::RgbaImage::new(10, 10));
        Arc::new(RenderImage::new(SmallVec::from_elem(frame, 1)))
    };

    let p1 = PathBuf::from("1.png");
    let p2 = PathBuf::from("2.png");
    let p3 = PathBuf::from("3.png");

    cache.put(p1.clone(), dummy_img(), 400);
    cache.put(p2.clone(), dummy_img(), 400);

    // contains should NOT update LRU order
    assert!(cache.contains(&p1));
    assert!(cache.contains(&p2));

    // Access p1 so p2 is older
    assert!(cache.get(&p1).is_some());

    // Insert p3 -> should evict p2
    cache.put(p3.clone(), dummy_img(), 400);
    assert!(cache.contains(&p1));
    assert!(!cache.contains(&p2));
    assert!(cache.contains(&p3));
}

#[test]
fn test_watcher_event_mapping() {
    use frame::watcher::{DirEvent, map_event};
    use notify::event::{CreateKind, ModifyKind, RemoveKind, RenameMode};
    use notify::{Event, EventKind};

    let p1 = PathBuf::from("img1.png");
    let p2 = PathBuf::from("img2.png");
    let p_txt = PathBuf::from("notes.txt");

    // Create
    let ev = Event {
        kind: EventKind::Create(CreateKind::File),
        paths: vec![p1.clone(), p_txt.clone()],
        attrs: Default::default(),
    };
    assert_eq!(map_event(ev), vec![DirEvent::Created(p1.clone())]);

    // Remove
    let ev = Event {
        kind: EventKind::Remove(RemoveKind::File),
        paths: vec![p1.clone()],
        attrs: Default::default(),
    };
    assert_eq!(map_event(ev), vec![DirEvent::Removed(p1.clone())]);

    // Rename Both
    let ev = Event {
        kind: EventKind::Modify(ModifyKind::Name(RenameMode::Both)),
        paths: vec![p1.clone(), p2.clone()],
        attrs: Default::default(),
    };
    assert_eq!(
        map_event(ev),
        vec![DirEvent::Renamed {
            from: p1.clone(),
            to: p2.clone()
        }]
    );

    // Rename to unsupported format
    let ev = Event {
        kind: EventKind::Modify(ModifyKind::Name(RenameMode::Both)),
        paths: vec![p1.clone(), p_txt.clone()],
        attrs: Default::default(),
    };
    assert_eq!(map_event(ev), vec![DirEvent::Removed(p1.clone())]);
}

#[test]
#[allow(clippy::field_reassign_with_default)]
fn test_viewer_reset_clears_animation_and_images() {
    use frame::viewer::ViewerState;
    use std::path::Path;

    let mut viewer = ViewerState::default();
    viewer.is_animated = true;
    viewer.is_thumbnail = true;

    viewer.reset_for_path(Path::new("test.png"));

    assert_eq!(viewer.current_path, Some(PathBuf::from("test.png")));
    assert!(!viewer.is_animated);
    assert!(!viewer.is_thumbnail);
    assert!(viewer.base_image.is_none());
    assert!(viewer.display_image.is_none());
}
