use crate::app_state::is_supported_image;
use notify::event::{ModifyKind, RenameMode};
use notify::{Event, EventKind, RecommendedWatcher, RecursiveMode, Watcher};
use std::path::{Path, PathBuf};

pub struct DirWatcher {
    _watcher: RecommendedWatcher,
    pub rx: async_channel::Receiver<DirEvent>,
}

#[derive(Debug, PartialEq, Eq, Clone)]
pub enum DirEvent {
    Created(PathBuf),
    Removed(PathBuf),
    Renamed { from: PathBuf, to: PathBuf },
    Modified(PathBuf),
}

pub fn map_event(event: Event) -> Vec<DirEvent> {
    let mut results = Vec::new();
    match event.kind {
        EventKind::Create(_) => {
            for path in event.paths {
                if is_supported_image(&path) {
                    results.push(DirEvent::Created(path));
                }
            }
        }
        EventKind::Remove(_) => {
            for path in event.paths {
                if is_supported_image(&path) {
                    results.push(DirEvent::Removed(path));
                }
            }
        }
        EventKind::Modify(ModifyKind::Name(mode)) => match mode {
            RenameMode::Both => {
                if event.paths.len() == 2 {
                    let from = event.paths[0].clone();
                    let to = event.paths[1].clone();
                    let from_sup = is_supported_image(&from);
                    let to_sup = is_supported_image(&to);

                    if from_sup && to_sup {
                        results.push(DirEvent::Renamed { from, to });
                    } else if from_sup && !to_sup {
                        results.push(DirEvent::Removed(from));
                    } else if !from_sup && to_sup {
                        results.push(DirEvent::Created(to));
                    }
                }
            }
            RenameMode::From => {
                for path in event.paths {
                    if is_supported_image(&path) {
                        results.push(DirEvent::Removed(path));
                    }
                }
            }
            RenameMode::To => {
                for path in event.paths {
                    if is_supported_image(&path) {
                        results.push(DirEvent::Created(path));
                    }
                }
            }
            _ => {
                if event.paths.len() == 2 {
                    let from = event.paths[0].clone();
                    let to = event.paths[1].clone();
                    let from_sup = is_supported_image(&from);
                    let to_sup = is_supported_image(&to);

                    if from_sup && to_sup {
                        results.push(DirEvent::Renamed { from, to });
                    } else if from_sup && !to_sup {
                        results.push(DirEvent::Removed(from));
                    } else if !from_sup && to_sup {
                        results.push(DirEvent::Created(to));
                    }
                }
            }
        },
        EventKind::Modify(ModifyKind::Data(_)) => {
            for path in event.paths {
                if is_supported_image(&path) {
                    results.push(DirEvent::Modified(path));
                }
            }
        }
        _ => {}
    }
    results
}

impl DirWatcher {
    pub fn new(dir: &Path) -> Option<Self> {
        if std::env::var_os("FRAME_TEST").is_some() {
            return None;
        }

        let (tx, rx) = async_channel::unbounded();

        let mut watcher = RecommendedWatcher::new(
            move |res: Result<Event, _>| {
                if let Ok(event) = res {
                    for dir_event in map_event(event) {
                        let _ = tx.send_blocking(dir_event);
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
