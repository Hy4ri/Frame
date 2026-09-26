use frame::cli::CliArgs;
use frame::frame_app::FrameApp;
use frame::keybindings;
use gpui::{Bounds, TitlebarOptions, WindowBounds, WindowOptions, px, size};
use gpui_kit::component::Root;
use gpui_kit::*;

fn main() {
    env_logger::init();

    let args = match CliArgs::parse() {
        Ok(args) => args,
        Err(code) => std::process::exit(code),
    };

    gpui_kit::application().run(move |cx| {
        gpui_kit::init(cx);
        gpui_component::theme::Theme::change(gpui_component::theme::ThemeMode::Dark, None, cx);
        let colors = &mut gpui_component::theme::Theme::global_mut(cx).colors;
        colors.background = gpui::rgb(0x121212).into();
        colors.foreground = gpui::rgb(0xFFFFFF).into();
        colors.popover = gpui::rgb(0x1B1B1B).into();
        colors.popover_foreground = gpui::rgb(0xFFFFFF).into();
        colors.input = gpui::rgb(0x242424).into();
        colors.border = gpui::rgb(0x343434).into();
        colors.button = gpui::rgb(0x242424).into();
        colors.button_foreground = gpui::rgb(0xFFFFFF).into();
        colors.button_hover = gpui::rgb(0x343434).into();
        colors.button_active = gpui::rgb(0x1B1B1B).into();
        colors.muted = gpui::rgb(0x242424).into();
        colors.muted_foreground = gpui::rgb(0xB3B3B3).into();
        colors.accent = gpui::rgb(0x990000).into();
        colors.accent_foreground = gpui::rgb(0xFFFFFF).into();
        colors.primary = gpui::rgb(0x990000).into();
        colors.primary_foreground = gpui::rgb(0xFFFFFF).into();
        colors.primary_hover = gpui::rgb(0xB30000).into();
        colors.primary_active = gpui::rgb(0x750000).into();
        let theme = gpui_component::theme::Theme::global_mut(cx);
        theme.radius = gpui::px(0.0);
        theme.radius_lg = gpui::px(0.0);
        gpui_component::theme::Theme::sync_base(cx);
        keybindings::register(cx);

        let initial_path = args.initial_path.clone();

        cx.open_window(
            WindowOptions {
                window_bounds: Some(WindowBounds::Windowed(Bounds::centered(
                    None,
                    size(px(1200.0), px(800.0)),
                    cx,
                ))),
                titlebar: Some(TitlebarOptions {
                    title: Some("Frame".into()),
                    ..Default::default()
                }),
                app_id: Some("frame".into()),
                ..Default::default()
            },
            |window, cx| {
                let view = cx.new(|cx| FrameApp::new(initial_path, window, cx));
                cx.new(|cx| Root::new(view, window, cx))
            },
        )
        .expect("Failed to open window");
    });
}
