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

// Incremental decoding: the crate's reader resumes after running out of input (an UnexpectedEof
// from the source), as Chromium uses it. The source reads a buffer that grows as data arrives.
use std::cell::RefCell;
use std::io::{BufRead, Read, Seek, SeekFrom};
use std::rc::Rc;

struct Source {
    data: Rc<RefCell<Vec<u8>>>,
    position: usize,
}

impl Read for Source {
    fn read(&mut self, out: &mut [u8]) -> std::io::Result<usize> {
        let available = self.fill_buf()?;
        let n = available.len().min(out.len());
        out[..n].copy_from_slice(&available[..n]);
        self.consume(n);
        Ok(n)
    }
}

impl BufRead for Source {
    fn fill_buf(&mut self) -> std::io::Result<&[u8]> {
        // Safe enough here: the Vec is only grown between decoder calls, never during one.
        let data = unsafe { &*self.data.as_ptr() };
        Ok(&data[self.position.min(data.len())..])
    }
    fn consume(&mut self, n: usize) {
        self.position += n;
    }
}

impl Seek for Source {
    fn seek(&mut self, to: SeekFrom) -> std::io::Result<u64> {
        let len = self.data.borrow().len() as i64;
        self.position = match to {
            SeekFrom::Start(n) => n as i64,
            SeekFrom::End(n) => len + n,
            SeekFrom::Current(n) => self.position as i64 + n,
        } as usize;
        Ok(self.position as u64)
    }
}

pub struct Stream {
    data: Rc<RefCell<Vec<u8>>>,
    reader: Option<png::Reader<Source>>,
    ignore_checksums: bool,
    frame: Vec<u8>, // gray + alpha output, spread to RGBA when asked for
    rows: usize, // non-interlaced rows done, which come in order
}

#[no_mangle]
pub extern "C" fn rustpng_stream_create(ignore_checksums: bool) -> *mut Stream {
    Box::into_raw(Box::new(Stream { data: Rc::new(RefCell::new(Vec::new())), reader: None, ignore_checksums, frame: Vec::new(), rows: 0 }))
}

fn spread_gray_alpha(gray_alpha: &[u8], rgba: &mut [u8]) {
    for (pixel, ga) in rgba.chunks_exact_mut(4).zip(gray_alpha.chunks_exact(2)) {
        pixel.copy_from_slice(&[ga[0], ga[0], ga[0], ga[1]]);
    }
}

/// Appends `size` new bytes and decodes as far as they go, row by row into `rgba` (`rgba_size`
/// bytes), an interlaced image's passes splatted over the pixels they stand for, which is the
/// crate's way of showing a partial interlaced image. Returns 0 when the image is done, 1 when it
/// needs more data, -1 on error.
///
/// # Safety
/// `stream` is from rustpng_stream_create; `data` points to `size` bytes, `rgba` to `rgba_size`.
#[no_mangle]
pub unsafe extern "C" fn rustpng_stream_update(stream: *mut Stream, data: *const u8, size: usize, complete: bool, rgba: *mut u8, rgba_size: usize) -> i32 {
    let s = &mut *stream;
    s.data.borrow_mut().extend_from_slice(std::slice::from_raw_parts(data, size));
    let eof = |e: &png::DecodingError| matches!(e, png::DecodingError::IoError(io) if io.kind() == std::io::ErrorKind::UnexpectedEof);
    let more = if complete { -1 } else { 1 };
    if s.reader.is_none() {
        let mut decoder = png::Decoder::new(Source { data: s.data.clone(), position: 0 });
        decoder.set_transformations(png::Transformations::EXPAND | png::Transformations::STRIP_16 | png::Transformations::ALPHA);
        if s.ignore_checksums {
            decoder.ignore_checksums(true);
        }
        match decoder.read_info() {
            Ok(reader) => {
                if reader.output_color_type().0 == png::ColorType::GrayscaleAlpha {
                    s.frame = vec![0; reader.output_buffer_size().unwrap_or(0)];
                }
                s.reader = Some(reader);
            }
            Err(ref e) if eof(e) => return more,
            Err(_) => return -1,
        }
    }
    let Stream { reader, frame, rows, .. } = s;
    let reader = reader.as_mut().unwrap();
    let out = std::slice::from_raw_parts_mut(rgba, rgba_size);
    let (color, depth) = reader.output_color_type();
    if depth != png::BitDepth::Eight {
        return -1;
    }
    // RGBA goes straight into the caller's pixels; gray + alpha into its own.
    let (image, bits_per_pixel): (&mut [u8], u8) = match color {
        png::ColorType::Rgba if out.len() == reader.output_buffer_size().unwrap_or(0) => (out, 32),
        png::ColorType::GrayscaleAlpha => (frame.as_mut_slice(), 16),
        _ => return -1,
    };
    let stride = reader.output_line_size(reader.info().width).unwrap_or(0);
    loop {
        match reader.next_interlaced_row() {
            Ok(Some(row)) => match row.interlace() {
                png::InterlaceInfo::Adam7(info) => png::splat_interlaced_row(image, stride, row.data(), info, bits_per_pixel),
                png::InterlaceInfo::Null(_) => {
                    image[*rows * stride..][..row.data().len()].copy_from_slice(row.data());
                    *rows += 1;
                }
            },
            Ok(None) => break,
            Err(ref e) if eof(e) => return more,
            Err(_) => return -1,
        }
    }
    if color == png::ColorType::GrayscaleAlpha {
        spread_gray_alpha(frame, std::slice::from_raw_parts_mut(rgba, rgba_size));
    }
    0
}

/// Writes what's decoded so far to `rgba` (for gray + alpha, which isn't decoded into it).
///
/// # Safety
/// `stream` is from rustpng_stream_create; `rgba` points to `rgba_size` bytes.
#[no_mangle]
pub unsafe extern "C" fn rustpng_stream_snapshot(stream: *mut Stream, rgba: *mut u8, rgba_size: usize) {
    let s = &*stream;
    if !s.frame.is_empty() && s.frame.len() * 2 == rgba_size {
        spread_gray_alpha(&s.frame, std::slice::from_raw_parts_mut(rgba, rgba_size));
    }
}

/// # Safety
/// `stream` is from rustpng_stream_create, or null.
#[no_mangle]
pub unsafe extern "C" fn rustpng_stream_destroy(stream: *mut Stream) {
    if !stream.is_null() {
        drop(Box::from_raw(stream));
    }
}
