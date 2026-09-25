use exif::{In, Reader, Tag};
use std::fs::File;
use std::io::BufReader;
use std::path::Path;

pub fn get_exif_data(path: &Path) -> Option<Vec<(String, String)>> {
    let file = File::open(path).ok()?;
    let mut reader = BufReader::new(file);
    let exif = Reader::new().read_from_container(&mut reader).ok()?;

    let tags = [
        (Tag::Make, "Make"),
        (Tag::Model, "Model"),
        (Tag::DateTimeOriginal, "Date"),
        (Tag::ExposureTime, "Exposure"),
        (Tag::FNumber, "Aperture"),
        (Tag::PhotographicSensitivity, "ISO"),
        (Tag::Orientation, "Orientation"),
        (Tag::FocalLength, "Focal Length"),
        (Tag::Flash, "Flash"),
    ];

    let mut data = Vec::new();
    for (tag, label) in &tags {
        if let Some(field) = exif.get_field(*tag, In::PRIMARY) {
            data.push((label.to_string(), field.display_value().to_string()));
        }
    }

    if data.is_empty() { None } else { Some(data) }
}
