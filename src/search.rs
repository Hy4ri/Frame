use crate::app_state::AppState;
use crate::cache::ImageCache;
use crate::prefetch::Prefetcher;
use gpui::prelude::FluentBuilder;
use gpui::{
    canvas, div, px, rgb, AppContext, Context, Corners, EventEmitter, InteractiveElement,
    IntoElement, ParentElement, Render, SharedString, StatefulInteractiveElement, Styled, Subscription, Window,
};
use gpui_component::input::{Input, InputEvent, InputState};
use std::sync::Arc;

pub const GRID_COLS: usize = 5;
pub const GRID_ROWS: usize = 5;

pub struct SearchView {
    pub query: String,
    pub input_state: gpui::Entity<InputState>,
    pub filtered_indices: Vec<usize>,
    pub selected_grid_idx: usize,
    pub scroll_offset: usize,
    pub thumb_cache: Arc<ImageCache>,
    pub all_images: Vec<std::path::PathBuf>,
    _input_sub: Subscription,
}

pub enum SearchEvent {
    Select(usize),
    Close,
}

impl EventEmitter<SearchEvent> for SearchView {}

impl SearchView {
    pub fn new(
        window: &mut Window,
        cx: &mut Context<Self>,
        thumb_cache: Arc<ImageCache>,
        app_state: &AppState,
    ) -> Self {
        let input_state = cx.new(|cx| {
            InputState::new(window, cx).placeholder("Type to search images...")
        });

        input_state.update(cx, |input, cx| {
            input.focus(window, cx);
        });

        let app_state_images = app_state.images.clone();
        let input_sub = cx.subscribe(&input_state, move |this: &mut Self, entity, event: &InputEvent, cx| {
            match event {
                InputEvent::Change => {
                    let text = entity.read(cx).text().to_string();
                    this.query = text;
                    this.update_filter_with_images(&app_state_images);
                    cx.notify();
                }
                InputEvent::PressEnter { .. } => {
                    if let Some(&app_idx) = this.filtered_indices.get(this.selected_grid_idx) {
                        cx.emit(SearchEvent::Select(app_idx));
                    } else {
                        cx.emit(SearchEvent::Close);
                    }
                }
                _ => {}
            }
        });

        let mut search = Self {
            query: String::new(),
            input_state,
            filtered_indices: Vec::new(),
            selected_grid_idx: 0,
            scroll_offset: 0,
            thumb_cache,
            all_images: app_state.images.clone(),
            _input_sub: input_sub,
        };

        search.update_filter(app_state);
        search
    }

    pub fn update_filter(&mut self, app_state: &AppState) {
        self.update_filter_with_images(&app_state.images);
    }

    pub fn update_filter_with_images(&mut self, images: &[std::path::PathBuf]) {
        let q = self.query.to_ascii_lowercase();
        self.filtered_indices = images
            .iter()
            .enumerate()
            .filter_map(|(idx, path)| {
                let name = path
                    .file_name()
                    .and_then(|n| n.to_str())
                    .unwrap_or("")
                    .to_ascii_lowercase();
                if q.is_empty() || name.contains(&q) {
                    Some(idx)
                } else {
                    None
                }
            })
            .collect();

        self.selected_grid_idx = 0;
        self.scroll_offset = 0;
    }

    pub fn handle_key(
        &mut self,
        key: &str,
        app_state: &AppState,
        prefetcher: &Prefetcher,
        _window: &mut Window,
        cx: &mut Context<Self>,
    ) {
        match key {
            "escape" => {
                cx.emit(SearchEvent::Close);
            }
            "enter" => {
                if let Some(&app_idx) = self.filtered_indices.get(self.selected_grid_idx) {
                    cx.emit(SearchEvent::Select(app_idx));
                } else {
                    cx.emit(SearchEvent::Close);
                }
            }
            "left" | "h" => {
                if self.selected_grid_idx > 0 {
                    self.selected_grid_idx -= 1;
                    self.adjust_scroll();
                }
            }
            "right" | "l" => {
                if self.selected_grid_idx + 1 < self.filtered_indices.len() {
                    self.selected_grid_idx += 1;
                    self.adjust_scroll();
                }
            }
            "up" | "k" => {
                if self.selected_grid_idx >= GRID_COLS {
                    self.selected_grid_idx -= GRID_COLS;
                    self.adjust_scroll();
                }
            }
            "down" | "j" => {
                if self.selected_grid_idx + GRID_COLS < self.filtered_indices.len() {
                    self.selected_grid_idx += GRID_COLS;
                    self.adjust_scroll();
                }
            }
            _ => {}
        }

        self.adjust_scroll();
        self.request_thumbnails(app_state, prefetcher, cx);
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

    pub fn request_thumbnails(
        &self,
        app_state: &AppState,
        prefetcher: &Prefetcher,
        cx: &mut Context<Self>,
    ) {
        let start = self.scroll_offset * GRID_COLS;
        let count = GRID_COLS * GRID_ROWS;
        let mut paths = Vec::new();

        for i in 0..count {
            if let Some(&idx) = self.filtered_indices.get(start + i) {
                if let Some(path) = app_state.images.get(idx) {
                    paths.push(path.clone());
                }
            }
        }

        if !paths.is_empty() {
            prefetcher.prefetch_paths(paths, cx);
        }
    }
}

impl Render for SearchView {
    fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        let match_count = self.filtered_indices.len();
        let start_item = self.scroll_offset * GRID_COLS;
        let total_cells = GRID_COLS * GRID_ROWS;

        div()
            .id("search-overlay")
            .size_full()
            .bg(rgb(0x141414))
            .flex()
            .flex_col()
            .p_4()
            .gap_3()
            .child(
                div()
                    .w_full()
                    .h(px(50.0))
                    .bg(rgb(0x181818))
                    .border_1()
                    .border_color(rgb(0x990000))
                    .rounded_md()
                    .px_3()
                    .flex()
                    .items_center()
                    .justify_between()
                    .child(
                        div()
                            .flex_1()
                            .child(Input::new(&self.input_state)),
                    )
                    .child(
                        div()
                            .text_xs()
                            .text_color(rgb(0xAAAAAA))
                            .child(format!("{} matches", match_count)),
                    ),
            )
            .child(
                div()
                    .id("grid-container")
                    .size_full()
                    .flex_1()
                    .grid()
                    .grid_cols(GRID_COLS as u16)
                    .gap_2()
                    .children((0..total_cells).map(|i| {
                        let item_idx = start_item + i;
                        if item_idx < self.filtered_indices.len() {
                            let is_selected = item_idx == self.selected_grid_idx;
                            let app_idx = self.filtered_indices[item_idx];
                            let path = self.all_images.get(app_idx);
                            let thumb = path.and_then(|p| self.thumb_cache.get(p));

                            div()
                                .id(SharedString::from(format!("cell-{}", i)))
                                .size_full()
                                .rounded_md()
                                .border_1()
                                .when(is_selected, |s| {
                                    s.border_color(rgb(0x990000)).bg(rgb(0x3C0A0A))
                                })
                                .when(!is_selected, |s| {
                                    s.border_color(rgb(0x323232)).bg(rgb(0x191919))
                                })
                                .p_2()
                                .flex()
                                .flex_col()
                                .items_center()
                                .justify_center()
                                .on_click(cx.listener({
                                    let app_idx = app_idx;
                                    move |this, _, _win, cx| {
                                        cx.emit(SearchEvent::Select(app_idx));
                                        this.query.clear();
                                    }
                                }))
                                .child({
                                    if let Some(render_img) = thumb {
                                        div()
                                            .size_full()
                                            .flex_1()
                                            .child(
                                                canvas(
                                                    move |_bounds, _win, _cx| {},
                                                    move |bounds, (), win, _cx| {
                                                        let _ = win.paint_image(
                                                            bounds,
                                                            bounds,
                                                            Corners::default(),
                                                            render_img,
                                                            0,
                                                            false,
                                                        );
                                                    },
                                                )
                                                .size_full(),
                                            )
                                    } else {
                                        div()
                                            .size_full()
                                            .flex_1()
                                            .bg(rgb(0x222222))
                                            .rounded_sm()
                                    }
                                })
                                .child(
                                    div()
                                        .h(px(20.0))
                                        .text_xs()
                                        .text_color(rgb(0xCCCCCC))
                                        .child(format!("#{}", app_idx + 1)),
                                )
                        } else {
                            div()
                            .id(SharedString::from(format!("cell-empty-{}", i)))
                            .size_full()
                            .invisible()
                        }
                    })),
            )
    }
}
