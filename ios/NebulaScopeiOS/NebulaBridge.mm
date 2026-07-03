//
// NebulaBridge.mm — Objective-C++ implementation. This is the ONLY file that
// sees both worlds: it holds C++ objects (nb::FloatImage, astro params) in ObjC
// ivars and exposes plain ObjC/Swift methods. Compiled as Objective-C++ (.mm).
//
#import "NebulaBridge.h"
#include "cpp/RenderCore.hpp"
#include <vector>
#include <cmath>

// Optionally compiled with CFITSIO (define NEBULA_HAVE_CFITSIO=1 and link the
// library — see ios/README.md). Without it, FITS loading returns a clear error.
#ifdef NEBULA_HAVE_CFITSIO
#include "fitsio.h"
#endif

// ---------------------------------------------------------------------------
//  NBStretch
// ---------------------------------------------------------------------------
@implementation NBStretch {
    astro::ChannelStretch _cs[3];
    double _lo[3];
    double _hi[3];
    int    _channels;
}

- (instancetype)initWithChannels:(int)ch {
    if ((self = [super init])) {
        _channels = ch < 1 ? 1 : (ch > 3 ? 3 : ch);
        for (int c = 0; c < 3; ++c) { _cs[c] = astro::ChannelStretch{}; _lo[c] = 0.0; _hi[c] = 1.0; }
        _fn = NBStretchAsinh;
        _colormap = NBColormapGray;
        _ghsD = 1.6f; _ghsB = 6.0f; _ghsSP = 0.18f; _ghsLP = 0.0f; _ghsHP = 1.0f;
        _splitThreshold = 0.25f;
    }
    return self;
}

- (NSInteger)channels { return _channels; }

static inline int clampCh(NSInteger c) { return c < 0 ? 0 : (c > 2 ? 2 : (int)c); }

- (float)blackForChannel:(NSInteger)c { return (float)_cs[clampCh(c)].black; }
- (float)midForChannel:(NSInteger)c   { return (float)_cs[clampCh(c)].mid;   }
- (float)whiteForChannel:(NSInteger)c { return (float)_cs[clampCh(c)].white; }

- (void)setBlack:(float)v forChannel:(NSInteger)c {
    if (c < 0) { for (int k = 0; k < 3; ++k) _cs[k].black = v; }
    else _cs[clampCh(c)].black = v;
}
- (void)setMid:(float)v forChannel:(NSInteger)c {
    if (c < 0) { for (int k = 0; k < 3; ++k) _cs[k].mid = v; }
    else _cs[clampCh(c)].mid = v;
}
- (void)setWhite:(float)v forChannel:(NSInteger)c {
    if (c < 0) { for (int k = 0; k < 3; ++k) _cs[k].white = v; }
    else _cs[clampCh(c)].white = v;
}

// Internal accessors for NBImage.
- (const astro::ChannelStretch *)cs { return _cs; }
- (const double *)lo { return _lo; }
- (const double *)hi { return _hi; }
- (void)setChannel:(int)c black:(double)b mid:(double)m white:(double)w lo:(double)lo hi:(double)hi {
    _cs[c].black = b; _cs[c].mid = m; _cs[c].white = w; _lo[c] = lo; _hi[c] = hi;
}

- (id)copyWithZone:(NSZone *)zone {
    NBStretch *s = [[NBStretch allocWithZone:zone] initWithChannels:_channels];
    s.fn = _fn; s.colormap = _colormap;
    s.ghsD = _ghsD; s.ghsB = _ghsB; s.ghsSP = _ghsSP; s.ghsLP = _ghsLP; s.ghsHP = _ghsHP;
    s.splitThreshold = _splitThreshold;
    for (int c = 0; c < 3; ++c) [s setChannel:c black:_cs[c].black mid:_cs[c].mid white:_cs[c].white lo:_lo[c] hi:_hi[c]];
    return s;
}
@end

// Category so NBImage can reach the private accessors above.
@interface NBStretch (Internal)
- (const astro::ChannelStretch *)cs;
- (const double *)lo;
- (const double *)hi;
- (void)setChannel:(int)c black:(double)b mid:(double)m white:(double)w lo:(double)lo hi:(double)hi;
- (instancetype)initWithChannels:(int)ch;
@end

// ---------------------------------------------------------------------------
//  NBImage
// ---------------------------------------------------------------------------
@implementation NBImage {
    nb::FloatImage _img;
}

- (NSInteger)width    { return _img.w; }
- (NSInteger)height   { return _img.h; }
- (NSInteger)channels { return _img.ch; }

+ (NBImage *)sampleGalaxyWithWidth:(NSInteger)w height:(NSInteger)h {
    NBImage *o = [[NBImage alloc] init];
    o->_img = nb::makeSampleGalaxy((int)w, (int)h);
    return o;
}

+ (nullable NBImage *)imageFromFITSPath:(NSString *)path error:(NSString **)err {
#ifdef NEBULA_HAVE_CFITSIO
    fitsfile *fp = nullptr; int status = 0;
    if (fits_open_file(&fp, path.fileSystemRepresentation, READONLY, &status)) {
        if (err) *err = @"Could not open FITS file"; return nil;
    }
    int naxis = 0; long naxes[3] = {1, 1, 1};
    fits_get_img_dim(fp, &naxis, &status);
    fits_get_img_size(fp, naxis > 3 ? 3 : naxis, naxes, &status);
    if (status || naxis < 2) { fits_close_file(fp, &status); if (err) *err = @"No 2-D image in primary HDU"; return nil; }

    const int w = (int)naxes[0], h = (int)naxes[1];
    const int ch = (naxis >= 3 && naxes[2] == 3) ? 3 : 1;
    NBImage *o = [[NBImage alloc] init];
    o->_img.w = w; o->_img.h = h; o->_img.ch = ch;
    o->_img.data.assign((size_t)w * h * ch, 0.0f);

    long fpixel[3] = {1, 1, 1};
    float nan = NAN;
    for (int c = 0; c < ch; ++c) {
        fpixel[2] = c + 1;
        fits_read_pix(fp, TFLOAT, fpixel, (LONGLONG)w * h, &nan,
                      o->_img.plane(c), nullptr, &status);
    }
    fits_close_file(fp, &status);
    if (status) { if (err) *err = @"FITS read error"; return nil; }
    return o;
#else
    if (err) *err = @"This build has no CFITSIO — see ios/README.md to enable FITS loading.";
    return nil;
#endif
}

- (NBStretch *)autoStretch {
    NBStretch *s = [[NBStretch alloc] initWithChannels:(int)_img.ch];
    s.fn = NBStretchAsinh;
    const int n = _img.ch;
    for (int c = 0; c < n; ++c) {
        nb::Stats st = nb::computeStats(_img, c);
        astro::ChannelStretch cs; double lo, hi;
        nb::computeSTF(st, cs, lo, hi);
        [s setChannel:c black:cs.black mid:cs.mid white:cs.white lo:lo hi:hi];
    }
    // Mono: mirror channel 0 into all three so render/window logic is uniform.
    if (n == 1) {
        nb::Stats st = nb::computeStats(_img, 0);
        astro::ChannelStretch cs; double lo, hi; nb::computeSTF(st, cs, lo, hi);
        for (int c = 1; c < 3; ++c) [s setChannel:c black:cs.black mid:cs.mid white:cs.white lo:lo hi:hi];
    }
    return s;
}

- (nullable UIImage *)renderWithStretch:(NBStretch *)stretch {
    const astro::StretchFn fn = (astro::StretchFn)stretch.fn;
    astro::GHSParams ghs; ghs.D = stretch.ghsD; ghs.b = stretch.ghsB;
    ghs.SP = stretch.ghsSP; ghs.LP = stretch.ghsLP; ghs.HP = stretch.ghsHP;

    // NBColormap is a UI-facing subset; map explicitly to astro::Colormap
    // (whose enum order differs — Split is not index 3).
    astro::Colormap cmap = astro::Colormap::Gray;
    switch (stretch.colormap) {
        case NBColormapHeat:    cmap = astro::Colormap::Heat;    break;
        case NBColormapViridis: cmap = astro::Colormap::Viridis; break;
        case NBColormapSplit:   cmap = astro::Colormap::Split;   break;
        case NBColormapGray:
        default:                cmap = astro::Colormap::Gray;    break;
    }

    std::vector<std::uint8_t> rgba = nb::renderRGBA(
        _img, fn, [stretch cs], [stretch lo], [stretch hi],
        ghs, cmap, stretch.splitThreshold);

    const int w = _img.w, h = _img.h;
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(rgba.data(), w, h, 8, w * 4, cs,
        kCGImageAlphaNoneSkipLast | kCGBitmapByteOrderDefault);
    UIImage *ui = nil;
    if (ctx) {
        CGImageRef cg = CGBitmapContextCreateImage(ctx);
        if (cg) { ui = [UIImage imageWithCGImage:cg]; CGImageRelease(cg); }
        CGContextRelease(ctx);
    }
    CGColorSpaceRelease(cs);
    return ui;
}

- (NSArray<NSNumber *> *)histogramForChannel:(NSInteger)c
                                        bins:(NSInteger)bins
                                    logScale:(BOOL)logScale
                                     stretch:(NBStretch *)stretch {
    const int ci = c < 0 ? 0 : (c >= _img.ch ? _img.ch - 1 : (int)c);
    std::vector<float> hb = nb::histogram(_img, ci, (int)bins, logScale,
                                          [stretch lo][ci], [stretch hi][ci]);
    NSMutableArray<NSNumber *> *out = [NSMutableArray arrayWithCapacity:bins];
    for (float v : hb) [out addObject:@(v)];
    return out;
}

- (BOOL)pixelAtX:(NSInteger)x y:(NSInteger)y outR:(float *)r outG:(float *)g outB:(float *)b {
    if (x < 0 || y < 0 || x >= _img.w || y >= _img.h) return NO;
    const size_t i = (size_t)y * _img.w + x;
    const float v0 = _img.plane(0)[i];
    if (_img.ch >= 3) { *r = v0; *g = _img.plane(1)[i]; *b = _img.plane(2)[i]; }
    else { *r = *g = *b = v0; }
    return YES;
}
@end
