const std = @import("std");
const assert = std.debug.assert;
const testing = std.testing;

const URLError = error{UnsupportedScheme};

// (Real code would use std.Uri instead.)
const URL = struct {
    scheme: []const u8,
    host: []const u8,
    path: []const u8,

    fn init(url: []const u8) URLError!URL {
        var it = std.mem.splitSequence(u8, url, "://");

        const scheme = it.first();
        if (!std.mem.eql(u8, scheme, "http"))
            return URLError.UnsupportedScheme;

        const rest = it.rest();
        const path_start = std.mem.indexOf(u8, rest, "/");
        const host = if (path_start == null) rest else rest[0..path_start.?];
        const path = if (path_start == null) "/" else rest[path_start.?..];

        return URL{
            .scheme = scheme,
            .host = host,
            .path = (if (path.len == 0) "/" else path),
        };
    }
};

test URL {
    {
        const url = try URL.init("http://example.com");
        try testing.expect(std.mem.eql(u8, url.scheme, "http"));
        try testing.expect(std.mem.eql(u8, url.host, "example.com"));
        try testing.expect(std.mem.eql(u8, url.path, "/"));
    }

    {
        const url = try URL.init("http://example.com/");
        try testing.expect(std.mem.eql(u8, url.scheme, "http"));
        try testing.expect(std.mem.eql(u8, url.host, "example.com"));
        try testing.expect(std.mem.eql(u8, url.path, "/"));
    }

    {
        const url = try URL.init("http://example.com/foo/bar/baz.html");
        try testing.expect(std.mem.eql(u8, url.scheme, "http"));
        try testing.expect(std.mem.eql(u8, url.host, "example.com"));
        try testing.expect(std.mem.eql(u8, url.path, "/foo/bar/baz.html"));
    }

    try testing.expectError(URLError.UnsupportedScheme, URL.init(""));

    try testing.expectError(URLError.UnsupportedScheme, URL.init("example.com"));

    try testing.expectError(URLError.UnsupportedScheme, URL.init("httpx://example.com"));
}

pub fn main() !void {
    const url = if (std.os.argv.len >= 2) std.mem.span(std.os.argv[1]) else "http://example.com";

    const parsed_url = try URL.init(url);

    const stdout = std.io.getStdOut().writer();
    try stdout.print("hello, {s}\n", .{parsed_url.host});
}
