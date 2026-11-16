// SPDX-License-Identifier: GPL-2.0-only
#include "platform/ios_importer.h"
#include "platform/ios_ultimate_download.h"
#include "import_session.h"
#include <atomic>
#include <stdexcept>
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

// The packager checks this linked identity against the requested IPA mode.
extern "C" __attribute__((used, visibility("default")))
const char rb_blitz_ios_importer_identity[] = "RB_BLITZ_IOS_IMPORTER_V1";

namespace {
namespace setup = rb_blitz::installer::ios;
namespace portable = rb_blitz::installer::portable;
std::atomic<bool> g_ultimate{false};
bool g_completed_setup = false;  // UIKit thread only.
struct ImportState {
  std::atomic<bool> alive{true}, cancelled{false};
  std::unique_ptr<setup::ImportSession> session;
  std::function<void()> ready;
};
NSString* Text(const std::string& text) {
  return [[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding]
      ?: @"An unknown setup error occurred.";
}
}

@interface RBBlitzImportController : UIViewController <UIDocumentPickerDelegate> {
 @public
  std::shared_ptr<ImportState> state;
}
@property(nonatomic, strong) UIWindow* setupWindow;
@property(nonatomic, strong) UIStackView* actions;
@property(nonatomic, strong) UILabel* heading;
@property(nonatomic, strong) UILabel* detail;
@property(nonatomic, strong) UILabel* status;
@property(nonatomic, strong) UIProgressView* progress;
@property(nonatomic, strong) UIActivityIndicatorView* spinner;
@property(nonatomic, strong) UIButton* cancel;
@property(nonatomic, strong) NSFileCoordinator* sourceCoordinator;
@property(nonatomic) BOOL busy;
@property(nonatomic) BOOL baseReady;
@property(nonatomic) BOOL pickingUltimate;
@property(nonatomic) BOOL waitingForActivation;
@property(nonatomic) UIBackgroundTaskIdentifier backgroundTask;
- (void)checkExisting;
- (void)close;
@end

@implementation RBBlitzImportController
- (void)viewDidLoad {
  [super viewDidLoad];
  [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(applicationBecameActive:)
      name:UIApplicationDidBecomeActiveNotification object:nil];
  self.backgroundTask = UIBackgroundTaskInvalid;
  self.view.backgroundColor = [UIColor colorWithRed:0.035 green:0.05 blue:0.10 alpha:1];
  UIScrollView* scroll = [UIScrollView new];
  scroll.translatesAutoresizingMaskIntoConstraints = NO;
  [self.view addSubview:scroll];
  UIStackView* stack = [UIStackView new];
  stack.axis = UILayoutConstraintAxisVertical;
  stack.spacing = 14;
  stack.translatesAutoresizingMaskIntoConstraints = NO;
  [scroll addSubview:stack];
  UILayoutGuide* safe = self.view.safeAreaLayoutGuide;
  [NSLayoutConstraint activateConstraints:@[
    [scroll.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:24],
    [scroll.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor constant:-24],
    [scroll.topAnchor constraintEqualToAnchor:safe.topAnchor constant:12],
    [scroll.bottomAnchor constraintEqualToAnchor:safe.bottomAnchor constant:-12],
    [scroll.contentLayoutGuide.widthAnchor constraintEqualToAnchor:scroll.frameLayoutGuide.widthAnchor],
    [stack.centerXAnchor constraintEqualToAnchor:scroll.frameLayoutGuide.centerXAnchor],
    [stack.widthAnchor constraintLessThanOrEqualToConstant:600],
    [stack.widthAnchor constraintEqualToAnchor:scroll.frameLayoutGuide.widthAnchor],
    [stack.topAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.topAnchor constant:8],
    [stack.bottomAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.bottomAnchor constant:-8]
  ]];
  // The width equality yields to the readable width on larger iPads.
  for (NSLayoutConstraint* constraint in scroll.constraints)
    if (constraint.firstItem == stack && constraint.firstAttribute == NSLayoutAttributeWidth &&
        constraint.secondItem) constraint.priority = 750;
  self.heading = [self label:28];
  self.heading.font = [UIFont preferredFontForTextStyle:UIFontTextStyleTitle1];
  self.heading.text = @"Rock Band Blitz";
  [stack addArrangedSubview:self.heading];
  self.detail = [self label:17];
  self.detail.text = @"Checking your saved game import…";
  [stack addArrangedSubview:self.detail];
  self.status = [self label:15];
  self.status.textColor = UIColor.lightGrayColor;
  [stack addArrangedSubview:self.status];
  self.progress = [UIProgressView new];
  self.progress.progressTintColor = [UIColor colorWithRed:1 green:0.25 blue:0.5 alpha:1];
  [stack addArrangedSubview:self.progress];
  self.spinner = [[UIActivityIndicatorView alloc] initWithActivityIndicatorStyle:UIActivityIndicatorViewStyleMedium];
  [stack addArrangedSubview:self.spinner];
  self.actions = [UIStackView new];
  self.actions.axis = UILayoutConstraintAxisVertical;
  self.actions.spacing = 10;
  [stack addArrangedSubview:self.actions];
  self.cancel = [self button:@"Cancel" action:@selector(cancelWork)];
  [stack addArrangedSubview:self.cancel];
}
- (UILabel*)label:(CGFloat)size {
  UILabel* label = [UILabel new];
  label.textColor = UIColor.whiteColor;
  label.font = [UIFont systemFontOfSize:size];
  label.adjustsFontForContentSizeCategory = YES;
  label.numberOfLines = 0;
  return label;
}
- (UIButton*)button:(NSString*)title action:(SEL)action {
  UIButtonConfiguration* configuration = UIButtonConfiguration.filledButtonConfiguration;
  configuration.title = title;
  configuration.baseBackgroundColor = [UIColor colorWithRed:0.20 green:0.14 blue:0.40 alpha:1];
  configuration.contentInsets = NSDirectionalEdgeInsetsMake(11, 18, 11, 18);
  UIButton* button = [UIButton buttonWithConfiguration:configuration primaryAction:nil];
  [button addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];
  return button;
}
- (void)showChoices {
  self.busy = NO;
  self.cancel.hidden = YES;
  self.progress.hidden = YES;
  [self.spinner stopAnimating];
  for (UIView* view in self.actions.arrangedSubviews) [view removeFromSuperview];
  self.actions.hidden = NO;
  if (self.baseReady) {
    self.heading.text = @"Make it Ultimate?";
    self.detail.text = @"Your game is ready. Rock Band Blitz Ultimate is an optional community mod. Import your own copy or download the supported release from GitHub.";
    [self.actions addArrangedSubview:[self button:@"Play without Ultimate" action:@selector(skipUltimate)]];
    [self.actions addArrangedSubview:[self button:@"Import Ultimate ZIP" action:@selector(pickUltimateFile)]];
    [self.actions addArrangedSubview:[self button:@"Import Ultimate folder" action:@selector(pickUltimateFolder)]];
    [self.actions addArrangedSubview:[self button:@"Download Ultimate from GitHub" action:@selector(downloadUltimate)]];
  } else {
    self.heading.text = @"Welcome to Rock Band Blitz";
    self.detail.text = @"Choose your Xbox 360 game package or extracted game folder in Files. Setup prepares it once, then starts the game. Keep your original file; future launches use the saved import.";
    [self.actions addArrangedSubview:[self button:@"Choose game file" action:@selector(pickGameFile)]];
    [self.actions addArrangedSubview:[self button:@"Choose extracted game folder" action:@selector(pickGameFolder)]];
  }
}
- (void)beginWork:(NSString*)detail {
  self.busy = YES;
  state->cancelled = false;
  self.actions.hidden = YES;
  self.cancel.hidden = NO;
  self.cancel.enabled = YES;
  self.status.text = detail;
  self.progress.progress = 0;
  self.progress.hidden = NO;
  [self.spinner startAnimating];
  auto shared = state;
  __weak RBBlitzImportController* weakSelf = self;
  self.backgroundTask = [UIApplication.sharedApplication beginBackgroundTaskWithName:@"Prepare Rock Band Blitz"
      expirationHandler:^{
        shared->cancelled = true;
        [weakSelf.sourceCoordinator cancel];
        [weakSelf endWork];
      }];
}
- (void)endWork {
  if (self.backgroundTask != UIBackgroundTaskInvalid) {
    const UIBackgroundTaskIdentifier task = self.backgroundTask;
    self.backgroundTask = UIBackgroundTaskInvalid;
    [UIApplication.sharedApplication endBackgroundTask:task];
  }
}
- (void)cancelWork {
  state->cancelled = true;
  [self.sourceCoordinator cancel];
  self.cancel.enabled = NO;
  self.status.text = @"Canceling safely…";
}
- (portable::ImportCallbacks)callbacks {
  auto shared = state;
  __weak RBBlitzImportController* weakSelf = self;
  portable::ImportCallbacks callbacks;
  callbacks.cancelled = [shared] { return shared->cancelled.load() || !shared->alive.load(); };
  callbacks.progress = [weakSelf, shared](int percent, std::string_view detail) {
    NSString* message = Text(std::string(detail));
    dispatch_async(dispatch_get_main_queue(), ^{
      RBBlitzImportController* controller = weakSelf;
      if (!controller || !shared->alive || !controller.busy) return;
      controller.progress.progress = float(percent) / 100;
      controller.status.text = message;
    });
  };
  return callbacks;
}
- (void)checkExisting {
  [self beginWork:@"Verifying saved game data…"];
  self.cancel.hidden = YES;
  auto shared = state;
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
    std::string error;
    bool ready = false, base = false;
    try {
      ready = shared->session->Open(&error);
      base = shared->session->BaseReady();
    } catch (const std::exception& exception) { error = exception.what(); }
    dispatch_async(dispatch_get_main_queue(), ^{
      [self endWork];
      if (!shared->alive) return;
      self.baseReady = base;
      if (ready) [self finishReady:@""];
      else {
        [self showChoices];
        self.status.text = base ? @"Your base game import has been preserved." : @"No game data is included in this app.";
        // Fresh installs have no marker; an existing damaged import has a
        // useful diagnostic. All errors remain recoverable through the picker.
        if (!error.empty() && std::filesystem::exists(shared->session->GameRoot()))
          self.status.text = Text(error);
      }
    });
  });
}
- (void)pick:(BOOL)ultimate folder:(BOOL)folder {
  if (self.busy) return;
  self.pickingUltimate = ultimate;
  // Open in place and keep security-scoped access until the coordinated worker
  // finishes copying. This also supports extensionless LIVE/STFS packages.
  UIDocumentPickerViewController* picker = [[UIDocumentPickerViewController alloc]
      initForOpeningContentTypes:@[folder ? UTTypeFolder : UTTypeItem] asCopy:NO];
  picker.delegate = self;
  picker.allowsMultipleSelection = NO;
  picker.modalPresentationStyle = UIModalPresentationFormSheet;
  [self presentViewController:picker animated:YES completion:nil];
}
- (void)pickGameFile { [self pick:NO folder:NO]; }
- (void)pickGameFolder { [self pick:NO folder:YES]; }
- (void)pickUltimateFile { [self pick:YES folder:NO]; }
- (void)pickUltimateFolder { [self pick:YES folder:YES]; }
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController*)controller {
  self.status.text = @"Selection canceled. Choose a file when you are ready.";
}
- (void)documentPicker:(UIDocumentPickerViewController*)controller didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
  if (!urls.count || self.busy) return;
  [self runChoice:setup::UltimateChoice::kLocal source:urls.firstObject ultimate:self.pickingUltimate];
}
- (void)skipUltimate { [self runChoice:setup::UltimateChoice::kSkip source:nil ultimate:YES]; }
- (void)downloadUltimate { [self runChoice:setup::UltimateChoice::kDownload source:nil ultimate:YES]; }
- (void)runChoice:(setup::UltimateChoice)choice source:(NSURL*)source ultimate:(BOOL)ultimate {
  if (self.busy) return;
  BOOL scoped = source ? [source startAccessingSecurityScopedResource] : NO;
  NSFileCoordinator* coordinator = source ? [[NSFileCoordinator alloc] initWithFilePresenter:nil] : nil;
  self.sourceCoordinator = coordinator;
  [self beginWork:ultimate ? @"Preparing your game…" : @"Importing your game…"];
  auto shared = state;
  const auto callbacks = [self callbacks];
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
    std::string error, warning;
    bool success = false;
    auto work = [&](const std::filesystem::path& path) {
      if (ultimate) success = shared->session->Finish(choice, path, callbacks, &warning, &error);
      else success = shared->session->ImportSource(path, callbacks, &error);
    };
    try {
      if (source) {
        NSError* coordinationError = nil;
        [coordinator coordinateReadingItemAtURL:source options:0 error:&coordinationError
            byAccessor:^(NSURL* readable) { work(readable.fileSystemRepresentation); }];
        if (coordinationError) error = coordinationError.localizedDescription.UTF8String;
      } else work({});
    } catch (const std::exception& exception) { error = exception.what(); }
    if (scoped) [source stopAccessingSecurityScopedResource];
    const bool base = shared->session->BaseReady();
    dispatch_async(dispatch_get_main_queue(), ^{
      [self endWork];
      self.sourceCoordinator = nil;
      if (!shared->alive) return;
      self.baseReady = base;
      if (success && ultimate) {
        g_completed_setup = true;
        [self finishReady:Text(warning)];
      }
      else {
        [self showChoices];
        self.status.text = success ? @"Game imported and verified." : Text(error);
      }
    });
  });
}
- (void)finishReady:(NSString*)warning {
  self.busy = NO;
  self.actions.hidden = YES;
  self.cancel.hidden = YES;
  self.progress.progress = 1;
  self.status.text = @"Your game is ready. Starting Rock Band Blitz…";
  g_ultimate = state->session->Choice() != setup::UltimateChoice::kSkip;
  if (warning.length) {
    UIAlertController* alert = [UIAlertController alertControllerWithTitle:@"Ultimate version"
        message:warning preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"Continue" style:UIAlertActionStyleDefault
        handler:^(UIAlertAction*) { [self requestLaunch]; }]];
    [self presentViewController:alert animated:YES completion:nil];
  } else [self requestLaunch];
}
- (void)requestLaunch {
  self.waitingForActivation = YES;
  [self applicationBecameActive:nil];
}
- (void)applicationBecameActive:(NSNotification*)notification {
  if (!self.waitingForActivation || !state->alive ||
      UIApplication.sharedApplication.applicationState != UIApplicationStateActive) return;
  self.waitingForActivation = NO;
  if (state->ready) state->ready();
}
- (void)close {
  [NSNotificationCenter.defaultCenter removeObserver:self];
  state->alive = false;
  state->cancelled = true;
  [self.sourceCoordinator cancel];
  state->ready = {};
  [self endWork];
  [self dismissViewControllerAnimated:NO completion:nil];
  self.setupWindow.hidden = YES;
  self.setupWindow.rootViewController = nil;
  self.setupWindow = nil;
}
@end

namespace rb_blitz::platform {
std::filesystem::path IOSImportedGameRoot() {
  NSURL* support = [NSFileManager.defaultManager URLForDirectory:NSApplicationSupportDirectory
      inDomain:NSUserDomainMask appropriateForURL:nil create:YES error:nil];
  if (!support) throw std::runtime_error("Cannot locate Application Support for the game import");
  return std::filesystem::path(support.fileSystemRepresentation) / "Rock Band Blitz" / "imported-game";
}
bool IOSImportUsesUltimate() { return g_ultimate.load(); }
bool IOSImportCompletedSetup() { return g_completed_setup; }
namespace {
class Importer final : public IOSImporter {
 public:
  explicit Importer(RBBlitzImportController* controller) : controller_(controller) {}
  ~Importer() override { [controller_ close]; }
  bool PrepareLaunch() override {
    if (UIApplication.sharedApplication.applicationState == UIApplicationStateActive) return true;
    [controller_ requestLaunch];
    return false;
  }
 private:
  RBBlitzImportController* __strong controller_;
};
}
std::unique_ptr<IOSImporter> StartIOSImporter(std::function<void()> ready) {
  auto shared = std::make_shared<ImportState>();
  auto config = setup::EmbeddedImportSessionConfig();
  config.ultimate.download = DownloadIOSUltimate;
  const auto root = IOSImportedGameRoot();
  std::filesystem::create_directories(root.parent_path());
  NSURL* directory = [NSURL fileURLWithFileSystemRepresentation:root.parent_path().c_str()
      isDirectory:YES relativeToURL:nil];
  [directory setResourceValue:@YES forKey:NSURLIsExcludedFromBackupKey error:nil];
  shared->session = std::make_unique<setup::ImportSession>(root, std::move(config));
  shared->ready = std::move(ready);
  RBBlitzImportController* controller = [RBBlitzImportController new];
  controller->state = shared;
  UIWindowScene* windowScene = nil;
  for (UIScene* scene in UIApplication.sharedApplication.connectedScenes) {
    if ([scene isKindOfClass:UIWindowScene.class] && scene.activationState != UISceneActivationStateUnattached) {
      windowScene = (UIWindowScene*)scene;
      break;
    }
  }
  UIWindow* window = windowScene ? [[UIWindow alloc] initWithWindowScene:windowScene]
                               : [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
  window.rootViewController = controller;
  controller.setupWindow = window;
  [window makeKeyAndVisible];
  [controller checkExisting];
  return std::make_unique<Importer>(controller);
}
}  // namespace rb_blitz::platform
