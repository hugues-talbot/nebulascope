// Native save panel with accessory view. See MacSavePanel.h for the design.
// Compiled without ARC (project default): everything lives inside the modal
// call, retained by the panel's view hierarchy; the helper is released
// explicitly after runModal.
#include "app/MacSavePanel.h"

#include <QGuiApplication>

#import <AppKit/AppKit.h>

namespace {

NSString* toNs(const QString& s) { return s.toNSString(); }

} // namespace

// Controller: format-popup reactions and the two save-panel delegate duties
// (keep foreign images clickable, adopt clicked names as base names, and
// force the final suffix to follow the chosen format).
@interface NSSavePanelHelper : NSObject <NSOpenSavePanelDelegate> {
  @public
    NSSavePanel*   panel;
    NSPopUpButton* formatPopup;
    NSPopUpButton* optionPopup;      // nil when the spec has none
    NSSlider*      slider;           // nil when the spec has none
    NSTextField*   sliderValueField;
    NSTextField*   optionLabel;
    NSTextField*   sliderLabel;
    // Per-format data mirrored from the spec:
    NSMutableArray<NSArray<NSString*>*>* formatSuffixes;   // lowercase, no dot
    NSMutableIndexSet* popupEnabledFormats;
    NSMutableIndexSet* sliderEnabledFormats;
    NSSet<NSString*>*  clickable;    // extensions enabled for name adoption
}
- (void)formatChanged:(id)sender;
- (void)sliderMoved:(id)sender;
- (NSString*)currentDefaultSuffix;
@end

@implementation NSSavePanelHelper

- (NSString*)currentDefaultSuffix {
    const NSInteger i = formatPopup.indexOfSelectedItem;
    if (i < 0 || i >= (NSInteger)formatSuffixes.count) return @"";
    return formatSuffixes[(NSUInteger)i].firstObject;
}

- (void)formatChanged:(id)sender {
    (void)sender;
    const NSInteger i = formatPopup.indexOfSelectedItem;
    if (i < 0 || i >= (NSInteger)formatSuffixes.count) return;
    // The allowed types drive what the panel appends on save; the delegate's
    // shouldEnableURL below independently keeps other images clickable.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    panel.allowedFileTypes = formatSuffixes[(NSUInteger)i];
#pragma clang diagnostic pop
    const BOOL popupOn  = [popupEnabledFormats containsIndex:(NSUInteger)i];
    const BOOL sliderOn = [sliderEnabledFormats containsIndex:(NSUInteger)i];
    if (optionPopup) { optionPopup.enabled = popupOn; optionLabel.textColor =
        popupOn ? NSColor.labelColor : NSColor.disabledControlTextColor; }
    if (slider) {
        slider.enabled = sliderOn;
        sliderValueField.textColor =
            sliderOn ? NSColor.labelColor : NSColor.disabledControlTextColor;
        sliderLabel.textColor =
            sliderOn ? NSColor.labelColor : NSColor.disabledControlTextColor;
    }
    // A name typed under the previous format keeps only its base; the panel
    // re-appends the fresh suffix on save.
    NSString* name = panel.nameFieldStringValue;
    if (name.pathExtension.length &&
        [clickable containsObject:name.pathExtension.lowercaseString])
        panel.nameFieldStringValue = name.stringByDeletingPathExtension;
}

- (void)sliderMoved:(id)sender {
    (void)sender;
    sliderValueField.stringValue =
        [NSString stringWithFormat:@"%d", (int)lround(slider.doubleValue)];
}

// Keep every adoptable image clickable in the browser, not just the current
// format (the Qt dialog needed a QFileSystemModel override for this).
- (BOOL)panel:(id)sender shouldEnableURL:(NSURL*)url {
    (void)sender;
    NSNumber* isDir = nil;
    [url getResourceValue:&isDir forKey:NSURLIsDirectoryKey error:nil];
    if (isDir.boolValue) return YES;
    return [clickable containsObject:url.pathExtension.lowercaseString];
}

// Clicking an existing file: adopt its BASE name only — the extension
// belongs to the selected format. The panel writes the clicked name into the
// field itself, so strip on the next runloop turn (after its update).
- (void)panelSelectionDidChange:(id)sender {
    NSSavePanel* p = (NSSavePanel*)sender;
    dispatch_async(dispatch_get_main_queue(), ^{
        NSString* name = p.nameFieldStringValue;
        if (name.pathExtension.length &&
            [self->clickable containsObject:name.pathExtension.lowercaseString])
            p.nameFieldStringValue = name.stringByDeletingPathExtension;
    });
}

// Final guarantee at OK time: whatever ended up in the field, the saved
// suffix follows the chosen format ("M81.xisf" under PNG becomes "M81.png",
// never "M81.xisf.png").
- (NSString*)panel:(id)sender userEnteredFilename:(NSString*)filename
         confirmed:(BOOL)okFlag {
    (void)sender;
    if (!okFlag || !filename.length) return filename;
    const NSInteger i = formatPopup.indexOfSelectedItem;
    if (i < 0 || i >= (NSInteger)formatSuffixes.count) return filename;
    NSArray<NSString*>* ok = formatSuffixes[(NSUInteger)i];
    NSString* ext = filename.pathExtension.lowercaseString;
    if ([ok containsObject:ext]) return filename;   // e.g. clicked .jpg stays .jpg
    NSString* base = ext.length ? filename.stringByDeletingPathExtension : filename;
    return [base stringByAppendingPathExtension:ok.firstObject];
}

@end

namespace astro::mac {

bool savePanelAvailable() {
    return QGuiApplication::platformName() == QLatin1String("cocoa");
}

SavePanelResult runSavePanel(const SavePanelSpec& spec) {
    SavePanelResult out;
    if (!savePanelAvailable() || spec.formats.isEmpty()) return out;

    NSSavePanel* panel = [NSSavePanel savePanel];
    panel.message = toNs(spec.title);
    panel.canCreateDirectories = YES;
    panel.extensionHidden = NO;
    panel.showsHiddenFiles = NO;
    if (!spec.directory.isEmpty())
        panel.directoryURL = [NSURL fileURLWithPath:toNs(spec.directory)
                                        isDirectory:YES];
    if (!spec.suggestedName.isEmpty())
        panel.nameFieldStringValue = toNs(spec.suggestedName);

    NSSavePanelHelper* helper = [[NSSavePanelHelper alloc] init];
    helper->panel = panel;
    helper->formatSuffixes = [NSMutableArray array];
    helper->popupEnabledFormats = [NSMutableIndexSet indexSet];
    helper->sliderEnabledFormats = [NSMutableIndexSet indexSet];
    NSMutableSet<NSString*>* click = [NSMutableSet set];
    for (int i = 0; i < spec.formats.size(); ++i) {
        const SaveFormat& f = spec.formats[i];
        NSMutableArray<NSString*>* sufs = [NSMutableArray array];
        for (const QString& s : f.suffixes) {
            [sufs addObject:toNs(s.toLower())];
            [click addObject:toNs(s.toLower())];
        }
        [helper->formatSuffixes addObject:sufs];
        if (f.popupEnabled)  [helper->popupEnabledFormats addIndex:(NSUInteger)i];
        if (f.sliderEnabled) [helper->sliderEnabledFormats addIndex:(NSUInteger)i];
    }
    for (const QString& s : spec.clickableSuffixes)
        [click addObject:toNs(s.toLower())];
    helper->clickable = click;

    // ---- accessory view: one horizontal row of native controls -------------
    auto makeLabel = [](const QString& text) {
        NSTextField* l = [NSTextField labelWithString:toNs(text)];
        l.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];
        return l;
    };
    NSMutableArray<NSView*>* views = [NSMutableArray array];

    [views addObject:makeLabel(spec.formatLabel)];
    NSPopUpButton* fmt = [[[NSPopUpButton alloc] initWithFrame:NSZeroRect
                                                     pullsDown:NO] autorelease];
    for (const SaveFormat& f : spec.formats)
        [fmt addItemWithTitle:toNs(f.label)];
    [fmt selectItemAtIndex:qBound(0, spec.formatIndex, int(spec.formats.size()) - 1)];
    fmt.target = helper;
    fmt.action = @selector(formatChanged:);
    helper->formatPopup = fmt;
    [views addObject:fmt];

    if (!spec.popupLabel.isEmpty()) {
        NSTextField* pl = makeLabel(spec.popupLabel);
        helper->optionLabel = pl;
        [views addObject:pl];
        NSPopUpButton* opt = [[[NSPopUpButton alloc] initWithFrame:NSZeroRect
                                                         pullsDown:NO] autorelease];
        for (const QString& it : spec.popupItems)
            [opt addItemWithTitle:toNs(it)];
        if (!spec.popupItems.isEmpty())
            [opt selectItemAtIndex:qBound(0, spec.popupIndex,
                                          int(spec.popupItems.size()) - 1)];
        helper->optionPopup = opt;
        [views addObject:opt];
    }
    if (!spec.sliderLabel.isEmpty()) {
        NSTextField* sl = makeLabel(spec.sliderLabel);
        helper->sliderLabel = sl;
        [views addObject:sl];
        NSSlider* s = [NSSlider sliderWithValue:spec.sliderValue
                                       minValue:spec.sliderMin
                                       maxValue:spec.sliderMax
                                         target:helper
                                         action:@selector(sliderMoved:)];
        [s.widthAnchor constraintGreaterThanOrEqualToConstant:120].active = YES;
        helper->slider = s;
        [views addObject:s];
        NSTextField* val = makeLabel(QString::number(spec.sliderValue));
        [val.widthAnchor constraintGreaterThanOrEqualToConstant:28].active = YES;
        helper->sliderValueField = val;
        [views addObject:val];
    }

    NSStackView* stack = [NSStackView stackViewWithViews:views];
    stack.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    stack.spacing = 8;
    stack.edgeInsets = NSEdgeInsetsMake(10, 16, 10, 16);
    stack.translatesAutoresizingMaskIntoConstraints = NO;

    NSView* box = [[[NSView alloc] initWithFrame:NSZeroRect] autorelease];
    [box addSubview:stack];
    [NSLayoutConstraint activateConstraints:@[
        [stack.centerXAnchor constraintEqualToAnchor:box.centerXAnchor],
        [stack.topAnchor constraintEqualToAnchor:box.topAnchor],
        [stack.bottomAnchor constraintEqualToAnchor:box.bottomAnchor],
        [stack.leadingAnchor constraintGreaterThanOrEqualToAnchor:box.leadingAnchor],
    ]];
    const NSSize fit = stack.fittingSize;
    box.frame = NSMakeRect(0, 0, fit.width, fit.height);
    panel.accessoryView = box;

    panel.delegate = helper;
    [helper formatChanged:nil];                    // initial enable/allowed state

    const NSModalResponse resp = [panel runModal];
    panel.delegate = nil;

    out.accepted = (resp == NSModalResponseOK) && panel.URL != nil;
    if (out.accepted) {
        out.path = QString::fromNSString(panel.URL.path);
        out.formatIndex = int(fmt.indexOfSelectedItem);
        out.popupIndex = helper->optionPopup
            ? int(helper->optionPopup.indexOfSelectedItem) : spec.popupIndex;
        out.sliderValue = helper->slider
            ? int(lround(helper->slider.doubleValue)) : spec.sliderValue;
    }
    [helper release];
    return out;
}

} // namespace astro::mac
