#pragma once

#import <Cocoa/Cocoa.h>

/// What a tab shows, so the bar can be rebuilt without knowing about sessions.
@interface ArTermTabDescriptor : NSObject
@property( nonatomic ) NSString* title;
@property( nonatomic ) NSString* symbol; ///< SF Symbol name for the leading icon.
@property( nonatomic ) BOOL      busy;   ///< Draws a connecting indicator.
@end

@protocol ArTermTabBarDelegate <NSObject>
- (void)tabBarDidSelectIndex:(NSInteger)index;
- (void)tabBarDidCloseIndex:(NSInteger)index;
- (void)tabBarDidRequestNewTab;
@end

/// A browser-style tab strip: one button per tab with a title and a close box,
/// and a + at the end. Scrolls horizontally once the tabs stop fitting.
@interface ArTermTabBarView : NSView

@property( nonatomic, weak ) id<ArTermTabBarDelegate> delegate;

/// Replaces the whole strip. Cheap enough at the number of tabs anyone opens.
- (void)setTabs:(NSArray<ArTermTabDescriptor*>*)tabs selectedIndex:(NSInteger)selected;

@end
