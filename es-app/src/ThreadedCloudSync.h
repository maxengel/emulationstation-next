#pragma once

#include <string>
#include <thread>
#include "components/AsyncNotificationComponent.h"

// Runs a headless cloud sync command in the background with a native
// progress card, instead of taking over the screen with a console.
class ThreadedCloudSync
{
public:
	static void start(Window* window, const std::string& command, const std::string& title);
	static bool isRunning() { return mInstance != nullptr; }

private:
	void run();

	ThreadedCloudSync(Window* window, const std::string& command, const std::string& title);
	~ThreadedCloudSync();

	std::string					mCommand;
	std::string					mTitle;

	Window*						mWindow;
	AsyncNotificationComponent* mWndNotification;

	std::thread*				mHandle;
	static ThreadedCloudSync*	mInstance;
};
