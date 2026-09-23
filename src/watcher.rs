use crate::app_state::is_supported_image;
use notify::event::ModifyKind;
use notify::{Event, EventKind, RecommendedWatcher, RecursiveMode, Watcher};
use std::path::{Path, PathBuf};
use std::sync::mpsc;

pub struct DirWatcher {
    _watcher: RecommendedWatcher,
    pub rx: mpsc::Receiver<DirEvent>,
}

#[derive(Debug)]
pub enum DirEvent {
    Created(PathBuf),
    Removed(PathBuf),
    Renamed { from: PathBuf, to: PathBuf },
}

impl DirWatcher {
    pub fn new(dir: &Path) -> Option<Self> {
        let (tx, rx) = mpsc::channel();

        let mut watcher = RecommendedWatcher::new(
            move |res: Result<Event, _>| {
                if let Ok(event) = res {
                    match event.kind {
                        EventKind::Create(_) => {
                            for path in event.paths {
                                if is_supported_image(&path) {
                                    let _ = tx.send(DirEvent::Created(path));
                                }
                            }
                        }
                        EventKind::Remove(_) => {
                            for path in event.paths {
                                let _ = tx.send(DirEvent::Removed(path));
                            }
                        }
                        EventKind::Modify(ModifyKind::Name(_)) => {
                            if event.paths.len() == 2 {
                                let _ = tx.send(DirEvent::Renamed {
                                    from: event.paths[0].clone(),
                                    to: event.paths[1].clone(),
                                });
                            }
                        }
                        _ => {}
                    }
                }
            },
            notify::Config::default(),
        )
        .ok()?;

        watcher.watch(dir, RecursiveMode::NonRecursive).ok()?;
        Some(Self {
            _watcher: watcher,
            rx,
        })
    }
}
