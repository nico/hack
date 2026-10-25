// A C-callable wrapper around the png crate for png-bench: decodes to 8-bit RGBA, like the other
// decoders there. The crate expands palettes and low bit depths, strips 16-bit samples to 8 and
// adds alpha (Transformations EXPAND | STRIP_16 | ALPHA), which leaves RGBA, or gray+alpha that
// is spread to RGBA here.

use std::io::Cursor;

fn reader(data: &[u8], ignore_checksums: bool) -> Option<png::Reader<Cursor<&[u8]>>> {
    let mut decoder = png::Decoder::new(Cursor::new(data));
    decoder.set_transformations(png::Transformations::EXPAND | png::Transformations::STRIP_16 | png::Transformations::ALPHA);
    if ignore_checksums {
        decoder.ignore_checksums(true);
    }
    decoder.read_info().ok()
}

/// Writes the image's size; returns 0, or -1 when it's not a PNG the crate reads.
///
/// # Safety
/// `data` points to `size` readable bytes; `width` and `height` are writable.
#[no_mangle]
pub unsafe extern "C" fn rustpng_get_size(data: *const u8, size: usize, width: *mut u32, height: *mut u32) -> i32 {
    let data = std::slice::from_raw_parts(data, size);
    match reader(data, true) {
        Some(r) => {
            *width = r.info().width;
            *height = r.info().height;
            0
        }
        None => -1,
    }
}

/// Decodes into `rgba`, `width * height * 4` bytes. With `ignore_checksums` false, the crate's
/// defaults apply: chunk CRCs checked, the zlib Adler-32 not. Returns 0, or -1 on error.
///
/// # Safety
/// `data` points to `size` readable bytes, `rgba` to `rgba_size` writable ones.
#[no_mangle]
pub unsafe extern "C" fn rustpng_decode(data: *const u8, size: usize, ignore_checksums: bool, rgba: *mut u8, rgba_size: usize) -> i32 {
    let data = std::slice::from_raw_parts(data, size);
    let out = std::slice::from_raw_parts_mut(rgba, rgba_size);
    let Some(mut r) = reader(data, ignore_checksums) else { return -1 };
    let (color, depth) = r.output_color_type();
    let Some(needed) = r.output_buffer_size() else { return -1 };
    if depth != png::BitDepth::Eight {
        return -1;
    }
    match color {
        png::ColorType::Rgba if needed == rgba_size => {
            if r.next_frame(out).is_err() {
                return -1;
            }
        }
        png::ColorType::GrayscaleAlpha if needed * 2 == rgba_size => {
            let mut gray_alpha = vec![0; needed];
            if r.next_frame(&mut gray_alpha).is_err() {
                return -1;
            }
            for (pixel, ga) in out.chunks_exact_mut(4).zip(gray_alpha.chunks_exact(2)) {
                pixel.copy_from_slice(&[ga[0], ga[0], ga[0], ga[1]]);
            }
        }
        _ => return -1,
    }
    0
}
