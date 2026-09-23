use std::path::PathBuf;

pub const VERSION: &str = env!("CARGO_PKG_VERSION");

pub struct CliArgs {
    pub initial_path: Option<PathBuf>,
}

impl CliArgs {
    pub fn parse() -> Result<Self, i32> {
        let mut args = std::env::args().skip(1);
        if let Some(arg) = args.next() {
            if arg == "-v" || arg == "--version" {
                println!("Frame version {}", VERSION);
                return Err(0);
            }
            if arg == "-h" || arg == "--help" {
                println!("Usage: frame [PATH]");
                println!();
                println!("A fast, minimal image viewer with vim keybindings.");
                println!();
                println!("Arguments:");
                println!("  [PATH]  Path to an image or directory");
                println!();
                println!("Options:");
                println!("  -v, --version  Print version information");
                println!("  -h, --help     Print help");
                return Err(0);
            }
            Ok(Self {
                initial_path: Some(PathBuf::from(arg)),
            })
        } else {
            Ok(Self { initial_path: None })
        }
    }
}
