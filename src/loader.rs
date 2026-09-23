use anyhow::{Context, Result};
use gpui_kit::RenderImage;
use image::{codecs::gif::GifDecoder, AnimationDecoder, DynamicImage, Frame, ImageReader, RgbaImage};
use smallvec::SmallVec;
use std::fs::File;
use std::io::BufReader;
use std::path::Path;
use std::sync::Arc;

pub fn is_animated(path: &Path) -> bool {
    path.extension()
        .and_then(|ext| ext.to_str())
        .map(|ext| {
            let lower = ext.to_ascii_lowercase();
            lower == "gif" || lower == "apng"
        })
        .unwrap_or(false)
}

fn rgba_to_bgra(buffer: &mut RgbaImage) {
    for pixel in buffer.chunks_exact_mut(4) {
        pixel.swap(0, 2);
    }
}

pub fn load_static(path: &Path) -> Result<Arc<RenderImage>> {
    let reader = ImageReader::open(path)
        .with_context(|| format!("Failed to open image: {:?}", path))?
        .with_guessed_format()
        .with_context(|| format!("Failed to determine image format: {:?}", path))?;

    let img = reader
        .decode()
        .with_context(|| format!("Failed to decode image: {:?}", path))?;

    let mut rgba = img.to_rgba8();
    rgba_to_bgra(&mut rgba);

    let frame = Frame::new(rgba);
    let render_image = RenderImage::new(SmallVec::from_elem(frame, 1));
    Ok(Arc::new(render_image))
}

pub fn load_image(path: &Path) -> Result<Arc<RenderImage>> {
    if is_animated(path) {
        if let Ok(file) = File::open(path) {
            let reader = BufReader::new(file);
            if let Ok(decoder) = GifDecoder::new(reader) {
                if let Ok(raw_frames) = decoder.into_frames().collect_frames() {
                    if !raw_frames.is_empty() {
                        let mut frames = SmallVec::with_capacity(raw_frames.len());
                        for mut frame in raw_frames {
                            rgba_to_bgra(frame.buffer_mut());
                            frames.push(frame);
                        }
                        return Ok(Arc::new(RenderImage::new(frames)));
                    }
                }
            }
        }
    }

    load_static(path)
}

pub fn create_thumbnail(original: &RenderImage, max_dim: u32) -> Option<Arc<RenderImage>> {
    let bytes = original.as_bytes(0)?;
    let size = original.size(0);
    let w = size.width.0 as u32;
    let h = size.height.0 as u32;

    if w == 0 || h == 0 {
        return None;
    }

    let raw = bytes.to_vec();
    let img_buf = RgbaImage::from_raw(w, h, raw)?;
    let dynamic = DynamicImage::ImageRgba8(img_buf);

    let thumb = dynamic.thumbnail(max_dim, max_dim);
    let thumb_rgba = thumb.to_rgba8();

    let frame = Frame::new(thumb_rgba);
    Some(Arc::new(RenderImage::new(SmallVec::from_elem(frame, 1))))
}
