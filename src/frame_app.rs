use crate::actions::*;
use crate::app_state::AppState;
use crate::cache::ImageCache;
use crate::exif_info::get_exif_data;
use crate::file_ops::{rename_file, trash_file};
use crate::prefetch::Prefetcher;
use crate::search::{SearchEvent, SearchView};
use crate::utils::{format_file_size, format_from_ext};
use crate::viewer::ViewerState;
use crate::watcher::{DirEvent, DirWatcher};
use gpui::prelude::FluentBuilder;
use gpui::{
    canvas, div, px, rgb, AppContext, Context, Entity, FocusHandle, InteractiveElement, IntoElement,
    MouseButton, MouseDownEvent, MouseMoveEvent, MouseUpEvent, ParentElement, Render,
    ScrollWheelEvent, Styled, Window,
};
use gpui_component::button::{Button, ButtonVariant, ButtonVariants};
use gpui_component::dialog::{
    DialogAction, DialogButtonProps, DialogClose, DialogFooter,
};
use gpui_component::input::{Input, InputState};
use gpui_component::WindowExt;
use std::path::PathBuf;
use std::sync::Arc;
use std::time::Instant;

pub struct FrameApp {
    pub app_state: AppState,
    pub viewer: ViewerState,
    pub image_cache: Arc<ImageCache>,
    pub thumb_cache: Arc<ImageCache>,
    pub prefetcher: Prefetcher,
    pub watcher: Option<DirWatcher>,

    pub search_active: bool,
    pub search_view: Option<Entity<SearchView>>,

    pub info_dialog_open: bool,
    pub help_dialog_open: bool,

    pub g_sequence: bool,

    pub last_nav_time: Instant,
    pub nav_pending: bool,

    pub is_fullscreen: bool,
    pub focus_handle: FocusHandle,
}

impl FrameApp {
    pub fn new(initial_path: Option<PathBuf>, window: &mut Window, cx: &mut Context<Self>) -> Self {
        let app_state = AppState::new(initial_path);
        let image_cache = Arc::new(ImageCache::new(50, 128 * 1024 * 1024));
        let thumb_cache = Arc::new(ImageCache::new(500, 32 * 1024 * 1024));
        let prefetcher = Prefetcher::new(image_cache.clone(), thumb_cache.clone());

        let watcher = if let Some(path) = app_state.current_path() {
            path.parent().and_then(DirWatcher::new)
        } else if let Ok(cwd) = std::env::current_dir() {
            DirWatcher::new(&cwd)
        } else {
            None
        };

        let focus_handle = cx.focus_handle();
        focus_handle.focus(window, cx);

        let mut app = Self {
            app_state,
            viewer: ViewerState::default(),
            image_cache,
            thumb_cache,
            prefetcher,
            watcher,
            search_active: false,
            search_view: None,
            info_dialog_open: false,
            help_dialog_open: false,
            g_sequence: false,
            last_nav_time: Instant::now(),
            nav_pending: false,
            is_fullscreen: false,
            focus_handle,
        };

        if app.app_state.current_path().is_some() {
            app.load_current_image(window, cx);
        }

        app
    }

    fn update_title(&self, window: &mut Window) {
        if let Some(path) = self.app_state.current_path() {
            let name = path
                .file_name()
                .and_then(|n| n.to_str())
                .unwrap_or("Frame");
            let title = format!(
                "{} ({}/{}) - Frame",
                name,
                self.app_state.display_index(),
                self.app_state.image_count()
            );
            window.set_window_title(&title);
        } else {
            window.set_window_title("Frame");
        }
    }

    fn load_current_image(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        if let Some(path) = self.app_state.current_path().map(|p| p.to_path_buf()) {
            self.image_cache.pin(Some(&path));
            self.viewer.load_path(&path, &self.image_cache, &self.thumb_cache);
            self.update_title(window);
            self.trigger_prefetch(cx);
        }
    }

    fn trigger_prefetch(&self, cx: &mut Context<Self>) {
        if self.app_state.image_count() <= 1 {
            return;
        }

        let curr = match self.app_state.current_index {
            Some(i) => i,
            None => return,
        };

        let count = self.app_state.image_count();
        let mut paths = Vec::new();

        for d in 1..=5 {
            if curr + d < count {
                if let Some(p) = self.app_state.images.get(curr + d) {
                    paths.push(p.clone());
                }
            }
            if curr >= d {
                if let Some(p) = self.app_state.images.get(curr - d) {
                    paths.push(p.clone());
                }
            }
        }

        if !paths.is_empty() {
            self.prefetcher.prefetch_paths(paths, cx);
        }
    }

    fn do_nav(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        let now = Instant::now();
        let delta = now.duration_since(self.last_nav_time).as_millis();
        self.last_nav_time = now;

        if delta < 80 {
            if let Some(path) = self.app_state.current_path() {
                if self.thumb_cache.get(path).is_some() || self.image_cache.get(path).is_some() {
                    self.load_current_image(window, cx);
                }
            }
            self.nav_pending = true;
        } else {
            self.load_current_image(window, cx);
            self.nav_pending = false;
        }

        self.update_title(window);
        cx.notify();
    }

    pub fn on_next(&mut self, _: &NextImage, window: &mut Window, cx: &mut Context<Self>) {
        if self.app_state.next() {
            self.do_nav(window, cx);
        }
        self.g_sequence = false;
    }

    pub fn on_prev(&mut self, _: &PrevImage, window: &mut Window, cx: &mut Context<Self>) {
        if self.app_state.prev() {
            self.do_nav(window, cx);
        }
        self.g_sequence = false;
    }

    pub fn on_first(&mut self, _: &FirstImage, window: &mut Window, cx: &mut Context<Self>) {
        if self.app_state.first() {
            self.do_nav(window, cx);
        }
        self.g_sequence = false;
    }

    pub fn on_last(&mut self, _: &LastImage, window: &mut Window, cx: &mut Context<Self>) {
        if self.app_state.last() {
            self.do_nav(window, cx);
        }
        self.g_sequence = false;
    }

    pub fn on_toggle_fullscreen(&mut self, _: &ToggleFullscreen, _window: &mut Window, cx: &mut Context<Self>) {
        self.is_fullscreen = !self.is_fullscreen;
        self.viewer.needs_fit = true;
        cx.notify();
    }

    pub fn on_zoom_in(&mut self, _: &ZoomIn, _window: &mut Window, cx: &mut Context<Self>) {
        self.viewer.zoom_in();
        self.g_sequence = false;
        cx.notify();
    }

    pub fn on_zoom_out(&mut self, _: &ZoomOut, _window: &mut Window, cx: &mut Context<Self>) {
        self.viewer.zoom_out();
        self.g_sequence = false;
        cx.notify();
    }

    pub fn on_zoom_fit(&mut self, _: &ZoomFit, _window: &mut Window, cx: &mut Context<Self>) {
        self.viewer.zoom_fit();
        self.g_sequence = false;
        cx.notify();
    }

    pub fn on_zoom_original(&mut self, _: &ZoomOriginal, _window: &mut Window, cx: &mut Context<Self>) {
        self.viewer.zoom_original();
        self.g_sequence = false;
        cx.notify();
    }

    pub fn on_rotate_cw(&mut self, _: &RotateCW, _window: &mut Window, cx: &mut Context<Self>) {
        self.viewer.rotate(true);
        self.g_sequence = false;
        cx.notify();
    }

    pub fn on_rotate_ccw(&mut self, _: &RotateCCW, _window: &mut Window, cx: &mut Context<Self>) {
        self.viewer.rotate(false);
        self.g_sequence = false;
        cx.notify();
    }

    pub fn on_delete(&mut self, _: &DeleteImage, window: &mut Window, cx: &mut Context<Self>) {
        self.g_sequence = false;
        let path = match self.app_state.current_path() {
            Some(p) => p.to_path_buf(),
            None => return,
        };

        let file_name = path
            .file_name()
            .and_then(|n| n.to_str())
            .unwrap_or("image")
            .to_string();

        let path_clone = path.clone();
        let entity = cx.entity().clone();
        window.open_alert_dialog(cx, move |alert, _, _| {
            let to_trash = path_clone.clone();
            let entity_clone = entity.clone();
            alert
                .title(format!("Move \"{}\" to trash?", file_name))
                .description("The file will be moved to system trash.")
                .button_props(
                    DialogButtonProps::default()
                        .ok_text("Delete")
                        .ok_variant(ButtonVariant::Danger)
                        .on_ok(move |_, win, cx| {
                            let _ = trash_file(&to_trash);
                            entity_clone.update(cx, |this, cx| {
                                this.image_cache.remove(&to_trash);
                                this.thumb_cache.remove(&to_trash);
                                this.app_state.remove_current();
                                this.load_current_image(win, cx);
                                cx.notify();
                            });
                            true
                        }),
                )
        });
    }

    pub fn on_rename(&mut self, _: &RenameImage, window: &mut Window, cx: &mut Context<Self>) {
        self.g_sequence = false;
        let path = match self.app_state.current_path() {
            Some(p) => p.to_path_buf(),
            None => return,
        };

        let current_name = path
            .file_name()
            .and_then(|n| n.to_str())
            .unwrap_or("")
            .to_string();

        let input_state = cx.new(|cx| {
            InputState::new(window, cx).default_value(current_name)
        });

        let path_clone = path.clone();
        let input_clone = input_state.clone();
        let entity = cx.entity().clone();
        window.open_dialog(cx, move |dialog, _, _| {
            let p = path_clone.clone();
            let inp = input_clone.clone();
            let entity_clone = entity.clone();
            dialog
                .title("Rename Image")
                .child(Input::new(&inp))
                .footer(
                    DialogFooter::new()
                        .gap_2()
                        .child(DialogClose::new().child(Button::new("cancel").label("Cancel").outline()))
                        .child(
                            DialogAction::new().child(
                                Button::new("rename")
                                    .label("Rename")
                                    .primary()
                                    .on_click(move |_, win, cx| {
                                        let text = inp.read(cx).text().to_string();
                                        if !text.is_empty() {
                                            if let Ok(new_path) = rename_file(&p, &text) {
                                                entity_clone.update(cx, |this, cx| {
                                                    this.image_cache.rename(&p, new_path.clone());
                                                    this.thumb_cache.rename(&p, new_path.clone());
                                                    this.app_state.rename_current(new_path);
                                                    this.load_current_image(win, cx);
                                                    cx.notify();
                                                });
                                            }
                                        }
                                    }),
                            ),
                        ),
                )
        });
    }

    pub fn on_show_info(&mut self, _: &ShowInfo, _window: &mut Window, cx: &mut Context<Self>) {
        self.info_dialog_open = true;
        self.g_sequence = false;
        cx.notify();
    }

    pub fn on_show_help(&mut self, _: &ShowHelp, _window: &mut Window, cx: &mut Context<Self>) {
        self.help_dialog_open = true;
        self.g_sequence = false;
        cx.notify();
    }

    pub fn on_open_search(&mut self, _: &OpenSearch, window: &mut Window, cx: &mut Context<Self>) {
        self.g_sequence = false;
        let search_view = cx.new(|cx| {
            SearchView::new(window, cx, self.thumb_cache.clone(), &self.app_state)
        });

        cx.subscribe_in(&search_view, window, move |this: &mut Self, _, event: &SearchEvent, window, cx| match event {
            SearchEvent::Select(idx) => {
                this.app_state.set_index(*idx);
                this.search_active = false;
                this.search_view = None;
                this.load_current_image(window, cx);
                this.focus_handle.focus(window, cx);
                cx.notify();
            }
            SearchEvent::Close => {
                this.search_active = false;
                this.search_view = None;
                this.focus_handle.focus(window, cx);
                cx.notify();
            }
        })
        .detach();

        self.search_view = Some(search_view);
        self.search_active = true;
        cx.notify();
    }

    pub fn on_close_overlay(&mut self, _: &CloseOverlay, window: &mut Window, cx: &mut Context<Self>) {
        if self.search_active {
            if let Some(ref view) = self.search_view {
                view.update(cx, |_, cx| {
                    cx.emit(SearchEvent::Close);
                });
            }
            self.search_active = false;
            self.search_view = None;
        }
        self.info_dialog_open = false;
        self.help_dialog_open = false;
        self.g_sequence = false;
        self.focus_handle.focus(window, cx);
        cx.notify();
    }

    pub fn on_quit(&mut self, _: &Quit, _window: &mut Window, cx: &mut Context<Self>) {
        cx.quit();
    }
}

impl Render for FrameApp {
    fn render(&mut self, window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        if self.viewer.check_thumbnail_upgrade(&self.image_cache) {
            cx.notify();
        }

        if self.viewer.tick_animation() {
            cx.notify();
        }

        if self.nav_pending && self.last_nav_time.elapsed().as_millis() >= 80 {
            self.load_current_image(window, cx);
            self.nav_pending = false;
            cx.notify();
        }

        if self.g_sequence && self.last_nav_time.elapsed().as_millis() > 1000 {
            self.g_sequence = false;
        }

        let mut watcher_events = Vec::new();
        if let Some(ref watcher) = self.watcher {
            while let Ok(event) = watcher.rx.try_recv() {
                watcher_events.push(event);
            }
        }

        if !watcher_events.is_empty() {
            let mut changed = false;
            for event in watcher_events {
                match event {
                    DirEvent::Created(path) => {
                        self.app_state.insert_sorted(path);
                        changed = true;
                    }
                    DirEvent::Removed(path) => {
                        self.image_cache.remove(&path);
                        self.thumb_cache.remove(&path);
                        let was_current = self
                            .app_state
                            .current_path()
                            .map(|p| p == path)
                            .unwrap_or(false);
                        self.app_state.remove_path(&path);
                        if was_current {
                            self.load_current_image(window, cx);
                        }
                        changed = true;
                    }
                    DirEvent::Renamed { from, to } => {
                        self.image_cache.rename(&from, to.clone());
                        self.thumb_cache.rename(&from, to.clone());
                        self.app_state.rename_path(&from, to);
                        changed = true;
                    }
                }
            }
            if changed {
                self.update_title(window);
                cx.notify();
            }
        }

        let search_view = self.search_view.clone();
        let key_context = if self.search_active { "Search" } else { "Viewer" };

        div()
            .track_focus(&self.focus_handle)
            .key_context(key_context)
            .id("main-frame")
            .size_full()
            .bg(rgb(0x1E1E1E))
            .on_action(cx.listener(Self::on_next))
            .on_action(cx.listener(Self::on_prev))
            .on_action(cx.listener(Self::on_first))
            .on_action(cx.listener(Self::on_last))
            .on_action(cx.listener(Self::on_toggle_fullscreen))
            .on_action(cx.listener(Self::on_zoom_in))
            .on_action(cx.listener(Self::on_zoom_out))
            .on_action(cx.listener(Self::on_zoom_fit))
            .on_action(cx.listener(Self::on_zoom_original))
            .on_action(cx.listener(Self::on_rotate_cw))
            .on_action(cx.listener(Self::on_rotate_ccw))
            .on_action(cx.listener(Self::on_delete))
            .on_action(cx.listener(Self::on_rename))
            .on_action(cx.listener(Self::on_show_info))
            .on_action(cx.listener(Self::on_show_help))
            .on_action(cx.listener(Self::on_open_search))
            .on_action(cx.listener(Self::on_close_overlay))
            .on_action(cx.listener(Self::on_quit))
            .on_mouse_down(
                MouseButton::Left,
                cx.listener(|this, event: &MouseDownEvent, win, cx| {
                    if !this.search_active && !this.info_dialog_open && !this.help_dialog_open {
                        this.focus_handle.focus(win, cx);
                    }
                    this.viewer.begin_drag(event.position);
                    cx.notify();
                }),
            )
            .on_mouse_move(cx.listener(|this, event: &MouseMoveEvent, _win, cx| {
                if this.viewer.is_dragging {
                    this.viewer.do_drag(event.position);
                    cx.notify();
                }
            }))
            .on_mouse_up(
                MouseButton::Left,
                cx.listener(|this, _event: &MouseUpEvent, _win, cx| {
                    this.viewer.end_drag();
                    cx.notify();
                }),
            )
            .on_scroll_wheel(cx.listener(|this, event: &ScrollWheelEvent, _win, cx| {
                if !this.search_active {
                    let dy: f32 = event.delta.pixel_delta(px(1.0)).y.into();
                    this.viewer.scroll_zoom(event.position, dy);
                    cx.notify();
                }
            }))
            .child({
                let entity = cx.entity().clone();
                canvas(
                    move |_bounds, _window, _cx| {},
                    move |bounds, (), window, cx| {
                        entity.update(cx, |this, _cx| {
                            this.viewer.paint(bounds, window);
                        });
                    },
                )
                .size_full()
            })
            .when_some(search_view, |parent, view| {
                parent.child(
                    div()
                        .absolute()
                        .inset_0()
                        .on_key_down(cx.listener(|this, event: &gpui::KeyDownEvent, win, cx| {
                            if let Some(ref s_view) = this.search_view {
                                s_view.update(cx, |search, cx| {
                                    search.handle_key(
                                        &event.keystroke.key,
                                        &this.app_state,
                                        &this.prefetcher,
                                        win,
                                        cx,
                                    );
                                });
                            }
                        }))
                        .child(view),
                )
            })
            .when(self.info_dialog_open, |parent| {
                let path_opt = self.app_state.current_path();
                let dims = self.viewer.current_dimensions().unwrap_or((0, 0));
                let file_info = path_opt.map(|p| {
                    let name = p.file_name().and_then(|n| n.to_str()).unwrap_or("").to_string();
                    let ext = p.extension().and_then(|e| e.to_str()).unwrap_or("").to_string();
                    let fmt = format_from_ext(&ext);
                    let meta = std::fs::metadata(p).ok();
                    let size = meta.map(|m| format_file_size(m.len())).unwrap_or_default();
                    (name, size, fmt)
                });

                let (name, size, fmt) = file_info.unwrap_or_default();
                let exif_list = path_opt.and_then(|p| get_exif_data(p)).unwrap_or_default();

                parent.child(
                    div()
                        .absolute()
                        .inset_0()
                        .bg(rgb(0x101010))
                        .flex()
                        .items_center()
                        .justify_center()
                        .child(
                            div()
                                .w(px(500.0))
                                .bg(rgb(0x1E1E1E))
                                .border_1()
                                .border_color(rgb(0x444444))
                                .rounded_lg()
                                .p_4()
                                .flex()
                                .flex_col()
                                .gap_3()
                                .child(
                                    div()
                                        .text_lg()
                                        .font_weight(gpui::FontWeight::BOLD)
                                        .text_color(rgb(0xFFFFFF))
                                        .child("Image Information"),
                                )
                                .child(
                                    div()
                                        .flex()
                                        .flex_col()
                                        .gap_1()
                                        .text_sm()
                                        .child(format!("File: {}", name))
                                        .child(format!("Size: {}", size))
                                        .child(format!("Dimensions: {}x{}", dims.0, dims.1))
                                        .child(format!("Format: {}", fmt))
                                        .child(format!(
                                            "Index: {} / {}",
                                            self.app_state.display_index(),
                                            self.app_state.image_count()
                                        )),
                                )
                                .when(!exif_list.is_empty(), |p| {
                                    p.child(
                                        div()
                                            .mt_2()
                                            .text_sm()
                                            .font_weight(gpui::FontWeight::BOLD)
                                            .child("EXIF Data:"),
                                    )
                                    .children(exif_list.into_iter().map(|(k, v)| {
                                        div().text_xs().text_color(rgb(0xCCCCCC)).child(format!("{}: {}", k, v))
                                    }))
                                })
                                .child(
                                    div()
                                        .mt_4()
                                        .flex()
                                        .justify_end()
                                        .child(
                                            Button::new("close-info")
                                                .label("Close")
                                                .primary()
                                                .on_click(cx.listener(|this, _, win, cx| {
                                                    this.info_dialog_open = false;
                                                    this.focus_handle.focus(win, cx);
                                                    cx.notify();
                                                })),
                                        ),
                                ),
                        ),
                )
            })
            .when(self.help_dialog_open, |parent| {
                parent.child(
                    div()
                        .absolute()
                        .inset_0()
                        .bg(rgb(0x101010))
                        .flex()
                        .items_center()
                        .justify_center()
                        .child(
                            div()
                                .w(px(600.0))
                                .bg(rgb(0x1E1E1E))
                                .border_1()
                                .border_color(rgb(0x444444))
                                .rounded_lg()
                                .p_4()
                                .flex()
                                .flex_col()
                                .gap_3()
                                .child(
                                    div()
                                        .text_lg()
                                        .font_weight(gpui::FontWeight::BOLD)
                                        .text_color(rgb(0xFFFFFF))
                                        .child("Keyboard Shortcuts"),
                                )
                                .child(
                                    div()
                                        .flex()
                                        .gap_4()
                                        .child(
                                            div()
                                                .flex_1()
                                                .flex()
                                                .flex_col()
                                                .gap_1()
                                                .child(div().font_weight(gpui::FontWeight::BOLD).text_color(rgb(0xCC3333)).child("NAVIGATION"))
                                                .child("h / ← : Previous image")
                                                .child("l / → : Next image")
                                                .child("j / ↓ : Next image")
                                                .child("k / ↑ : Previous image")
                                                .child("G : Last image")
                                                .child("/ : Search grid")
                                                .child("q / Esc : Quit"),
                                        )
                                        .child(
                                            div()
                                                .flex_1()
                                                .flex()
                                                .flex_col()
                                                .gap_1()
                                                .child(div().font_weight(gpui::FontWeight::BOLD).text_color(rgb(0xCC3333)).child("VIEW & OPS"))
                                                .child("f : Fullscreen")
                                                .child("+ / = / z : Zoom in")
                                                .child("- / x : Zoom out")
                                                .child("0 : Fit to window")
                                                .child("1 : Original size")
                                                .child("r / R : Rotate CW/CCW")
                                                .child("d : Delete image")
                                                .child("F2 : Rename image")
                                                .child("i : Image info")
                                                .child("? : Help"),
                                        ),
                                )
                                .child(
                                    div()
                                        .mt_4()
                                        .flex()
                                        .justify_end()
                                        .child(
                                            Button::new("close-help")
                                                .label("Close")
                                                .primary()
                                                .on_click(cx.listener(|this, _, win, cx| {
                                                    this.help_dialog_open = false;
                                                    this.focus_handle.focus(win, cx);
                                                    cx.notify();
                                                })),
                                        ),
                                ),
                        ),
                )
            })
    }
}
