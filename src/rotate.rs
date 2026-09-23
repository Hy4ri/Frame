use gpui_kit::RenderImage;
use image::{Frame, RgbaImage};
use smallvec::SmallVec;
use std::sync::Arc;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum Rotation {
    #[default]
    R0,
    R90,
    R180,
    R270,
}

impl Rotation {
    pub fn next_cw(self) -> Self {
        match self {
            Self::R0 => Self::R90,
            Self::R90 => Self::R180,
            Self::R180 => Self::R270,
            Self::R270 => Self::R0,
        }
    }

    pub fn next_ccw(self) -> Self {
        match self {
            Self::R0 => Self::R270,
            Self::R270 => Self::R180,
            Self::R180 => Self::R90,
            Self::R90 => Self::R0,
        }
    }
}

pub fn rotate_render_image(img: &RenderImage, rot: Rotation) -> Option<Arc<RenderImage>> {
    if rot == Rotation::R0 {
        return None;
    }

    let bytes = img.as_bytes(0)?;
    let size = img.size(0);
    let w = size.width.0 as u32;
    let h = size.height.0 as u32;

    if w == 0 || h == 0 {
        return None;
    }

    let raw = bytes.to_vec();
    let src = RgbaImage::from_raw(w, h, raw)?;

    let dst = match rot {
        Rotation::R90 => image::imageops::rotate90(&src),
        Rotation::R180 => image::imageops::rotate180(&src),
        Rotation::R270 => image::imageops::rotate270(&src),
        Rotation::R0 => src,
    };

    let frame = Frame::new(dst);
    Some(Arc::new(RenderImage::new(SmallVec::from_elem(frame, 1))))
}
