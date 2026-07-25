#include "ThreadedCloudSync.h"
#include "Window.h"
#include "components/AsyncNotificationComponent.h"
#include "guis/GuiMsgBox.h"
#include "utils/Platform.h"
#include "LocaleES.h"

#define ICONINDEX _U("\uF0C2 ")

ThreadedCloudSync* ThreadedCloudSync::mInstance = nullptr;

ThreadedCloudSync::ThreadedCloudSync(Window* window, const std::string& command, const std::string& title)
	: mWindow(window), mCommand(command), mTitle(title)
{
	mWndNotification = mWindow->createAsyncNotificationComponent();
	mWndNotification->updateTitle(ICONINDEX + mTitle);
	mWndNotification->updateText(_("Syncing with the cloud..."));
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
	int ret = Utils::Platform::runSystemCommand(mCommand, "", nullptr);

	if (ret == 0)
		mWindow->displayNotificationMessage(ICONINDEX + mTitle + " : " + _("FINISHED"));
	else
		mWindow->displayNotificationMessage(ICONINDEX + mTitle + " : " + _("FAILED. SEE /var/log/cloud_sync.log"));

	delete this;
	ThreadedCloudSync::mInstance = nullptr;
}

void ThreadedCloudSync::start(Window* window, const std::string& command, const std::string& title)
{
	if (ThreadedCloudSync::mInstance != nullptr)
	{
		window->pushGui(new GuiMsgBox(window, _("A CLOUD SYNC IS ALREADY RUNNING.")));
		return;
	}

	ThreadedCloudSync::mInstance = new ThreadedCloudSync(window, command, title);
}
