use crate::app_state::AppState;
use crate::cache::ImageCache;
use crate::frame_app::theme;
use crate::prefetch::Prefetcher;
use fuzzy_matcher::FuzzyMatcher;
use fuzzy_matcher::skim::SkimMatcherV2;
use gpui::prelude::FluentBuilder;
use gpui::{
    AppContext, Bounds, Context, Corners, EventEmitter, InteractiveElement, IntoElement,
    ParentElement, Render, SharedString, Size, StatefulInteractiveElement, Styled, Subscription,
    Window, canvas, div, point, px,
};
use gpui_component::input::{Input, InputEvent, InputState};
use std::path::PathBuf;
use std::sync::Arc;

pub const GRID_COLS: usize = 5;
pub const GRID_ROWS: usize = 5;

pub struct SearchView {
    pub query: String,
    pub input_state: gpui::Entity<InputState>,
    pub filtered_paths: Vec<PathBuf>,
    pub selected_grid_idx: usize,
    pub scroll_offset: usize,
    pub thumb_cache: Arc<ImageCache>,
    pub prefetcher: Prefetcher,
    pub all_images: Vec<PathBuf>,
    matcher: SkimMatcherV2,
    _input_sub: Subscription,
}

pub enum SearchEvent {
    Select(PathBuf),
    Close,
}

impl EventEmitter<SearchEvent> for SearchView {}

impl SearchView {
    pub fn new(
        window: &mut Window,
        cx: &mut Context<Self>,
        thumb_cache: Arc<ImageCache>,
        app_state: &AppState,
        prefetcher: &Prefetcher,
    ) -> Self {
        let input_state =
            cx.new(|cx| InputState::new(window, cx).placeholder("Type to search images..."));

        input_state.update(cx, |input, cx| {
            input.focus(window, cx);
        });

        let input_sub = cx.subscribe(
            &input_state,
            move |this: &mut Self, entity, event: &InputEvent, cx| match event {
                InputEvent::Change => {
                    let text = entity.read(cx).text().to_string();
                    this.query = text;
                    this.update_filter();
                    this.request_thumbnails_internal(cx);
                    cx.notify();
                }
                InputEvent::PressEnter { .. } => {
                    if let Some(path) = this.filtered_paths.get(this.selected_grid_idx) {
                        cx.emit(SearchEvent::Select(path.clone()));
                    } else {
                        cx.emit(SearchEvent::Close);
                    }
                }
                _ => {}
            },
        );

        let mut search = Self {
            query: String::new(),
            input_state,
            filtered_paths: Vec::new(),
            selected_grid_idx: 0,
            scroll_offset: 0,
            thumb_cache,
            prefetcher: prefetcher.clone(),
            all_images: app_state.images.clone(),
            matcher: SkimMatcherV2::default(),
            _input_sub: input_sub,
        };

        search.update_filter();
        prefetcher.prefetch_thumbnails(search.visible_paths(), cx);
        search
    }

    pub fn sync_all_images(&mut self, images: &[PathBuf], cx: &mut Context<Self>) {
        self.all_images = images.to_vec();
        self.update_filter();
        self.request_thumbnails_internal(cx);
    }

    pub fn update_filter(&mut self) {
        let q = self.query.trim();
        if q.is_empty() {
            self.filtered_paths = self.all_images.clone();
        } else {
            let mut matches: Vec<(i64, PathBuf)> = self
                .all_images
                .iter()
                .filter_map(|path| {
                    let name = path.file_name().and_then(|n| n.to_str()).unwrap_or("");
                    self.matcher
                        .fuzzy_match(name, q)
                        .map(|score| (score, path.clone()))
                })
                .collect();

            matches.sort_by_key(|a| std::cmp::Reverse(a.0));
            self.filtered_paths = matches.into_iter().map(|(_, p)| p).collect();
        }

        self.selected_grid_idx = 0;
        self.scroll_offset = 0;
    }

    pub fn visible_paths(&self) -> Vec<PathBuf> {
        let start = self.scroll_offset * GRID_COLS;
        let count = GRID_COLS * GRID_ROWS;
        self.filtered_paths
            .iter()
            .skip(start)
            .take(count)
            .cloned()
            .collect()
    }

    fn request_thumbnails_internal(&self, cx: &mut Context<Self>) {
        let paths = self.visible_paths();
        if !paths.is_empty() {
            self.prefetcher.prefetch_thumbnails(paths, cx);
        }
    }

    pub fn handle_key(
        &mut self,
        key: &str,
        prefetcher: &Prefetcher,
        _window: &mut Window,
        cx: &mut Context<Self>,
    ) {
        match key {
            "left" => {
                if self.selected_grid_idx > 0 {
                    self.selected_grid_idx -= 1;
                    self.adjust_scroll();
                }
            }
            "right" => {
                if self.selected_grid_idx + 1 < self.filtered_paths.len() {
                    self.selected_grid_idx += 1;
                    self.adjust_scroll();
                }
            }
            "up" => {
                if self.selected_grid_idx >= GRID_COLS {
                    self.selected_grid_idx -= GRID_COLS;
                    self.adjust_scroll();
                }
            }
            "down" => {
                if self.selected_grid_idx + GRID_COLS < self.filtered_paths.len() {
                    self.selected_grid_idx += GRID_COLS;
                    self.adjust_scroll();
                }
            }
            "pageup" => {
                let step = GRID_COLS * GRID_ROWS;
                self.selected_grid_idx = self.selected_grid_idx.saturating_sub(step);
                self.adjust_scroll();
            }
            "pagedown" => {
                let step = GRID_COLS * GRID_ROWS;
                if !self.filtered_paths.is_empty() {
                    self.selected_grid_idx =
                        (self.selected_grid_idx + step).min(self.filtered_paths.len() - 1);
                    self.adjust_scroll();
                }
            }
            _ => return,
        }

        self.adjust_scroll();
        prefetcher.prefetch_thumbnails(self.visible_paths(), cx);
        cx.notify();
    }

    fn adjust_scroll(&mut self) {
        let sel_row = self.selected_grid_idx / GRID_COLS;
        if sel_row < self.scroll_offset {
            self.scroll_offset = sel_row;
        } else if sel_row >= self.scroll_offset + GRID_ROWS {
            self.scroll_offset = sel_row - GRID_ROWS + 1;
        }
    }
}

impl Render for SearchView {
    fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        let match_count = self.filtered_paths.len();
        let start_item = self.scroll_offset * GRID_COLS;
        let total_cells = GRID_COLS * GRID_ROWS;

        div()
            .id("search-overlay")
            .size_full()
            .bg(gpui::rgb(theme::BACKGROUND))
            .flex()
            .flex_col()
            .p_4()
            .gap_3()
            .child(
                div()
                    .flex()
                    .items_center()
                    .justify_between()
                    .child(
                        div()
                            .text_lg()
                            .font_weight(gpui::FontWeight::SEMIBOLD)
                            .text_color(gpui::rgb(theme::TEXT))
                            .child("Search"),
                    )
                    .child(div().text_xs().text_color(gpui::rgb(theme::MUTED)).child(
                        if match_count == 1 {
                            "1 image".to_string()
                        } else {
                            format!("{} images", match_count)
                        },
                    )),
            )
            .child(
                div()
                    .w_full()
                    .h(px(44.0))
                    .bg(gpui::rgb(theme::SURFACE))
                    .border_1()
                    .border_color(gpui::rgb(theme::ACCENT))
                    .px_3()
                    .flex()
                    .items_center()
                    .child(
                        div()
                            .flex_1()
                            .child(Input::new(&self.input_state).appearance(false)),
                    ),
            )
            .when(match_count == 0, |this| {
                this.child(
                    div()
                        .flex_1()
                        .flex()
                        .items_center()
                        .justify_center()
                        .text_sm()
                        .text_color(gpui::rgb(theme::MUTED))
                        .child("No images match"),
                )
            })
            .when(match_count > 0, |this| {
                this.child(
                    div()
                        .id("grid-container")
                        .w_full()
                        .flex_1()
                        .grid()
                        .grid_cols(GRID_COLS as u16)
                        .gap_2()
                        .children((0..total_cells).map(|i| {
                            let item_idx = start_item + i;
                            if item_idx < self.filtered_paths.len() {
                                let is_selected = item_idx == self.selected_grid_idx;
                                let path = self.filtered_paths[item_idx].clone();
                                let thumb = self.thumb_cache.get(&path);

                                let file_name = path
                                    .file_name()
                                    .and_then(|n| n.to_str())
                                    .unwrap_or("")
                                    .to_string();

                                let path_for_click = path.clone();

                                div()
                                    .id(SharedString::from(format!("cell-{}", i)))
                                    .size_full()
                                    .border_1()
                                    .when(is_selected, |s| {
                                        s.border_color(gpui::rgb(theme::ACCENT))
                                            .bg(gpui::rgb(theme::SURFACE_RAISED))
                                    })
                                    .when(!is_selected, |s| {
                                        s.border_color(gpui::rgb(theme::BORDER))
                                            .bg(gpui::rgb(theme::SURFACE))
                                    })
                                    .p_2()
                                    .flex()
                                    .flex_col()
                                    .items_center()
                                    .justify_center()
                                    .on_click(cx.listener({
                                        let target_path = path_for_click;
                                        move |this, _, _win, cx| {
                                            cx.emit(SearchEvent::Select(target_path.clone()));
                                            this.query.clear();
                                        }
                                    }))
                                    .child({
                                        if let Some(render_img) = thumb {
                                            div().w_full().flex_1().child(
                                                canvas(
                                                    move |_bounds, _win, _cx| {},
                                                    move |bounds, (), win, _cx| {
                                                        let img_size = render_img.size(0);
                                                        let iw = img_size.width.0 as f32;
                                                        let ih = img_size.height.0 as f32;
                                                        if iw > 0.0 && ih > 0.0 {
                                                            let bw: f32 = bounds.size.width.into();
                                                            let bh: f32 = bounds.size.height.into();
                                                            let scale = (bw / iw).min(bh / ih);
                                                            let rw = iw * scale;
                                                            let rh = ih * scale;
                                                            let ox = bounds.origin.x
                                                                + px((bw - rw) / 2.0);
                                                            let oy = bounds.origin.y
                                                                + px((bh - rh) / 2.0);

                                                            let dest = Bounds {
                                                                origin: point(ox, oy),
                                                                size: Size {
                                                                    width: px(rw),
                                                                    height: px(rh),
                                                                },
                                                            };

                                                            let _ = win.paint_image(
                                                                dest,
                                                                dest,
                                                                Corners::default(),
                                                                render_img,
                                                                0,
                                                                false,
                                                            );
                                                        }
                                                    },
                                                )
                                                .size_full(),
                                            )
                                        } else {
                                            div()
                                                .w_full()
                                                .flex_1()
                                                .bg(gpui::rgb(theme::SURFACE_RAISED))
                                        }
                                    })
                                    .child(
                                        div()
                                            .h(px(20.0))
                                            .w_full()
                                            .truncate()
                                            .text_xs()
                                            .text_center()
                                            .text_color(gpui::rgb(theme::MUTED))
                                            .child(file_name),
                                    )
                            } else {
                                div()
                                    .id(SharedString::from(format!("cell-empty-{}", i)))
                                    .w_full()
                                    .invisible()
                            }
                        })),
                )
            })
            .child(
                div()
                    .pt_2()
                    .border_t_1()
                    .border_color(gpui::rgb(theme::BORDER))
                    .flex()
                    .gap_4()
                    .text_xs()
                    .text_color(gpui::rgb(theme::MUTED))
                    .child(hint("Arrows", "Move"))
                    .child(hint("PgUp/PgDn", "Page"))
                    .child(hint("Enter", "Open"))
                    .child(hint("Esc", "Close")),
            )
    }
}

fn hint(key: &'static str, label: &'static str) -> gpui::Div {
    div()
        .flex()
        .items_center()
        .gap_1p5()
        .child(
            div()
                .px_1p5()
                .bg(gpui::rgb(theme::SURFACE_RAISED))
                .border_1()
                .border_color(gpui::rgb(theme::BORDER))
                .text_color(gpui::rgb(theme::TEXT))
                .child(key),
        )
        .child(label)
}
