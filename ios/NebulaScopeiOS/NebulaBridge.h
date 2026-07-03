//
// NebulaBridge.h — the Objective-C surface that Swift talks to. It hides all
// C++ (the RenderCore + Stretch/Colormap math) behind two plain ObjC classes:
//
//   NBStretch — the mutable stretch parameters (function, per-channel B/M/W,
//               display window, GHS controls, colormap). Swift edits this.
//   NBImage   — a loaded (or synthetic) Float32 image. Renders itself to a
//               UIImage through NBStretch, and yields histograms + pixel values.
//
#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, NBStretchFn) {
    NBStretchLinear = 0, NBStretchLog = 1, NBStretchAsinh = 2, NBStretchGHS = 3
};

// Subset of the desktop colormaps, enough for the PoC.
typedef NS_ENUM(NSInteger, NBColormap) {
    NBColormapGray = 0, NBColormapHeat = 1, NBColormapViridis = 2, NBColormapSplit = 3
};

@interface NBStretch : NSObject <NSCopying>
@property (nonatomic) NBStretchFn fn;
@property (nonatomic) NBColormap colormap;
@property (nonatomic, readonly) NSInteger channels;   // 1 or 3
// GHS controls (used when fn == NBStretchGHS)
@property (nonatomic) float ghsD, ghsB, ghsSP, ghsLP, ghsHP;
@property (nonatomic) float splitThreshold;           // Colormap Split break (0..1)

// Per-channel window points, each in [0,1] of the display range. For a mono
// image only channel 0 is meaningful.
- (float)blackForChannel:(NSInteger)c;
- (float)midForChannel:(NSInteger)c;
- (float)whiteForChannel:(NSInteger)c;
- (void)setBlack:(float)v forChannel:(NSInteger)c;   // linked when c < 0 (all channels)
- (void)setMid:(float)v forChannel:(NSInteger)c;
- (void)setWhite:(float)v forChannel:(NSInteger)c;
@end

@interface NBImage : NSObject
@property (nonatomic, readonly) NSInteger width, height, channels;

// A synthetic edge-on galaxy — the app opens on this so it runs with no files.
+ (NBImage *)sampleGalaxyWithWidth:(NSInteger)w height:(NSInteger)h;

// Load the primary HDU of a FITS file as Float32. Returns nil (with *err) when
// the build has no CFITSIO linked, or on failure. See ios/README.md.
+ (nullable NBImage *)imageFromFITSPath:(NSString *)path error:(NSString * _Nullable * _Nullable)err;

// Recommended auto-stretch (STF) parameters for this image.
- (NBStretch *)autoStretch;

// Render the stretched (and, for mono, colormapped) image to a UIImage.
- (nullable UIImage *)renderWithStretch:(NBStretch *)stretch;

// Normalized [0,1] histogram of the current display window for one channel.
- (NSArray<NSNumber *> *)histogramForChannel:(NSInteger)c
                                        bins:(NSInteger)bins
                                    logScale:(BOOL)logScale
                                     stretch:(NBStretch *)stretch;

// Raw pixel values at an integer coordinate (RGB; mono repeats into all three).
- (BOOL)pixelAtX:(NSInteger)x y:(NSInteger)y
            outR:(float *)r outG:(float *)g outB:(float *)b;
@end

NS_ASSUME_NONNULL_END
