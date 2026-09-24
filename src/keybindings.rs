use crate::actions::*;
use gpui::{App, KeyBinding};

pub fn register(cx: &mut App) {
    cx.bind_keys([
        KeyBinding::new("right", NextImage, Some("Viewer")),
        KeyBinding::new("l", NextImage, Some("Viewer")),
        KeyBinding::new("down", NextImage, Some("Viewer")),
        KeyBinding::new("j", NextImage, Some("Viewer")),
        KeyBinding::new("left", PrevImage, Some("Viewer")),
        KeyBinding::new("h", PrevImage, Some("Viewer")),
        KeyBinding::new("up", PrevImage, Some("Viewer")),
        KeyBinding::new("k", PrevImage, Some("Viewer")),
        KeyBinding::new("shift-g", LastImage, Some("Viewer")),
        KeyBinding::new("f", ToggleFullscreen, Some("Viewer")),
        KeyBinding::new("=", ZoomIn, Some("Viewer")),
        KeyBinding::new("z", ZoomIn, Some("Viewer")),
        KeyBinding::new("-", ZoomOut, Some("Viewer")),
        KeyBinding::new("x", ZoomOut, Some("Viewer")),
        KeyBinding::new("0", ZoomFit, Some("Viewer")),
        KeyBinding::new("1", ZoomOriginal, Some("Viewer")),
        KeyBinding::new("r", RotateCW, Some("Viewer")),
        KeyBinding::new("shift-r", RotateCCW, Some("Viewer")),
        KeyBinding::new("d", DeleteImage, Some("Viewer")),
        KeyBinding::new("delete", DeleteImage, Some("Viewer")),
        KeyBinding::new("f2", RenameImage, Some("Viewer")),
        KeyBinding::new("i", ShowInfo, Some("Viewer")),
        KeyBinding::new("shift-/", ShowHelp, Some("Viewer")),
        KeyBinding::new("/", OpenSearch, Some("Viewer")),
        KeyBinding::new("q", Quit, Some("Viewer")),
        KeyBinding::new("escape", CloseOverlay, Some("Viewer")),
        KeyBinding::new("escape", CloseOverlay, Some("Search")),
        ]);
}
