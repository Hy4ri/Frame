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
