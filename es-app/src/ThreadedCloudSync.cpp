#include "ThreadedCloudSync.h"
#include "Window.h"
#include "components/AsyncNotificationComponent.h"
#include "guis/GuiMsgBox.h"
#include "utils/Platform.h"
#include "utils/StringUtil.h"
#include <cstdio>
#include <sys/wait.h>
#include "LocaleES.h"

#define ICONINDEX _U("\uF0C2 ")

ThreadedCloudSync* ThreadedCloudSync::mInstance = nullptr;

ThreadedCloudSync::ThreadedCloudSync(Window* window, const std::string& command,
	const std::string& title, const std::string& running)
	: mWindow(window), mCommand(command), mTitle(title), mRunning(running)
{
	mWndNotification = mWindow->createAsyncNotificationComponent();
	mWndNotification->updateTitle(ICONINDEX + (mRunning.empty() ? mTitle : mRunning));
	mWndNotification->updateText(_("Working..."));
	mWndNotification->updatePercent(-1);

	mHandle = new std::thread(&ThreadedCloudSync::run, this);
}

ThreadedCloudSync::~ThreadedCloudSync()
{
	mWndNotification->close();
	mWndNotification = nullptr;

	ThreadedCloudSync::mInstance = nullptr;
}

void ThreadedCloudSync::run()
{
	// Stream the backend's output into the notification card so the user
	// sees live status (rclone --stats-one-line lines, phase banners, ...).
	int ret = -1;
	FILE* pipe = popen((mCommand + " 2>&1").c_str(), "r");
	if (pipe != nullptr)
	{
		char line[512];
		while (fgets(line, sizeof(line), pipe) != nullptr)
		{
			std::string text(line);

			// keep it single-line and printable
			std::string clean;
			for (char c : text)
				if (c >= 32 && c < 127)
					clean += c;

			clean = Utils::String::trim(clean);
			if (!clean.empty() && mWndNotification != nullptr)
				mWndNotification->updateText(clean);
		}

		int status = pclose(pipe);
		if (WIFEXITED(status))
			ret = WEXITSTATUS(status);
	}

	if (ret == 0)
		mWindow->displayNotificationMessage(ICONINDEX + mTitle + " : " + _("FINISHED"));
	else
		mWindow->displayNotificationMessage(ICONINDEX + mTitle + " : " + _("FAILED. SEE /var/log/cloud_sync.log"));

	delete this;
	ThreadedCloudSync::mInstance = nullptr;
}

void ThreadedCloudSync::start(Window* window, const std::string& command,
	const std::string& title, const std::string& running)
{
	if (ThreadedCloudSync::mInstance != nullptr)
	{
		window->pushGui(new GuiMsgBox(window, _("A CLOUD SYNC IS ALREADY RUNNING.")));
		return;
	}

	ThreadedCloudSync::mInstance = new ThreadedCloudSync(window, command, title, running);
}
