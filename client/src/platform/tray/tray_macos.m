// macOS menu-bar (NSStatusItem) tray icon, ARC-managed (see CMakeLists.txt's
// -fobjc-arc flag for this one file).
//
// Alternates a hand-rolled [NSApp run]/stop: cycle here with webview's own
// internal Cocoa run loop (ui_bridge_run -> webview_run) on the same shared
// NSApplication singleton across the process's lifetime. Nested run loops
// are a standard Cocoa pattern (e.g. modal panels), but this specific
// alternation needs verification on real hardware, not just a compile check.

#include "platform/tray.h"
#import <Cocoa/Cocoa.h>

@interface TinypadTrayDelegate : NSObject
@property(nonatomic, assign) tray_result_t result;
@end

@implementation TinypadTrayDelegate

// -stop: only takes effect once -run's loop checks it, which happens after
// processing the *next* event -- post a dummy one so that happens right away
// instead of waiting for the user to do something else first.
- (void)wakeRunLoop
{
    NSEvent *event = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                         location:NSZeroPoint
                                    modifierFlags:0
                                        timestamp:0
                                     windowNumber:0
                                          context:nil
                                          subtype:0
                                            data1:0
                                            data2:0];
    [NSApp postEvent:event atStart:YES];
}

- (void)onShow:(id)sender
{
    self.result = TRAY_RESULT_SHOW;
    [NSApp stop:nil];
    [self wakeRunLoop];
}

- (void)onQuit:(id)sender
{
    self.result = TRAY_RESULT_QUIT;
    [NSApp stop:nil];
    [self wakeRunLoop];
}

@end

tray_result_t tray_run(void)
{
    @autoreleasepool {
        [NSApplication sharedApplication];
        // No Dock icon/app switcher entry while backgrounded; restored to
        // Regular below if the user picks Show.
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

        NSStatusItem *item =
            [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
        item.button.title = @"TP"; // plain text, no bundled icon asset needed yet

        TinypadTrayDelegate *delegate = [[TinypadTrayDelegate alloc] init];
        delegate.result = TRAY_RESULT_ERROR; // only reached if -run returns without stop:

        NSMenu *menu = [[NSMenu alloc] init];
        NSMenuItem *show_item = [[NSMenuItem alloc] initWithTitle:@"Show"
                                                            action:@selector(onShow:)
                                                     keyEquivalent:@""];
        show_item.target = delegate;
        [menu addItem:show_item];

        NSMenuItem *quit_item = [[NSMenuItem alloc] initWithTitle:@"Quit"
                                                            action:@selector(onQuit:)
                                                     keyEquivalent:@""];
        quit_item.target = delegate;
        [menu addItem:quit_item];

        item.menu = menu;

        [NSApp run];

        [[NSStatusBar systemStatusBar] removeStatusItem:item];

        if (delegate.result == TRAY_RESULT_SHOW) {
            [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
            [NSApp activateIgnoringOtherApps:YES];
        }

        return delegate.result;
    }
}
