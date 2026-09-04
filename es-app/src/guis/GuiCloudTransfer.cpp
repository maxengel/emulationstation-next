#include "guis/GuiCloudTransfer.h"

#include "Window.h"
#include "ThemeData.h"
#include "LocaleES.h"
#include "utils/StringUtil.h"
#include "Log.h"

#include <cstdio>
#include <sys/wait.h>

GuiCloudTransfer::GuiCloudTransfer(Window* window, const std::string& command, const std::string& title)
	: GuiComponent(window), mBusyAnim(window), mBackground(window, ":/frame.png"),
	  mCommand(command), mTitleText(title), mPercent(-1), mFinished(false), mExit(-1),
	  mElapsedMs(0), mHandle(nullptr)
{
	auto theme = ThemeData::getMenuTheme();
	mBackground.setImagePath(theme->Background.path);
	mBackground.setEdgeColor(theme->Background.color);
	mBackground.setCenterColor(theme->Background.centerColor);
	mBackground.setCornerSize(theme->Background.cornerSize);

	setSize(Renderer::getScreenWidth(), Renderer::getScreenHeight());

	mTitle  = std::make_shared<TextComponent>(window, Utils::String::toUpper(title),
		theme->Title.font, theme->Title.color, ALIGN_CENTER);
	mStatus = std::make_shared<TextComponent>(window, _("STARTING..."),
		theme->Text.font, theme->Text.color, ALIGN_CENTER);
	mFooter = std::make_shared<TextComponent>(window, _("THIS CAN TAKE A WHILE. YOU CAN LEAVE IT RUNNING."),
		theme->TextSmall.font, theme->TextSmall.color, ALIGN_CENTER);

	const float w = Renderer::getScreenWidth() * 0.8f;
	const float cx = Renderer::getScreenWidth() * 0.5f;
	for (auto& t : { mTitle, mStatus, mFooter })
	{
		t->setSize(w, 0);
		t->setPosition(cx - w / 2.0f, 0);
	}
	mTitle->setPosition(cx - w / 2.0f,  Renderer::getScreenHeight() * 0.34f);
	mStatus->setPosition(cx - w / 2.0f, Renderer::getScreenHeight() * 0.46f);
	mFooter->setPosition(cx - w / 2.0f, Renderer::getScreenHeight() * 0.60f);

	mBackground.fitTo(Vector2f(w + Renderer::getScreenWidth() * 0.08f,
		Renderer::getScreenHeight() * 0.38f),
		Vector3f(cx - w / 2.0f - Renderer::getScreenWidth() * 0.04f,
			Renderer::getScreenHeight() * 0.30f, 0), Vector2f(-32, -32));

	mHandle = new std::thread(&GuiCloudTransfer::threadRun, this);
}

GuiCloudTransfer::~GuiCloudTransfer()
{
	if (mHandle != nullptr)
	{
		if (mHandle->joinable())
			mHandle->join();
		delete mHandle;
	}
}

// Input is refused while the transfer runs -- there is nothing to choose, and
// a stray press should not close a page somebody is waiting on. Once it has
// finished any button dismisses it, which is the whole point: the result waits
// for the person rather than the other way round.
bool GuiCloudTransfer::input(InputConfig* config, Input input)
{
	std::unique_lock<std::mutex> lock(mMutex);
	if (!mFinished || !input.value)
		return true;
	lock.unlock();
	delete this;
	return true;
}

std::vector<HelpPrompt> GuiCloudTransfer::getHelpPrompts()
{
	std::vector<HelpPrompt> prompts;
	std::unique_lock<std::mutex> lock(mMutex);
	if (mFinished)
		prompts.push_back(HelpPrompt("b", _("CLOSE")));
	return prompts;
}

void GuiCloudTransfer::render(const Transform4x4f& parentTrans)
{
	Transform4x4f trans = parentTrans * getTransform();
	Renderer::setMatrix(trans);
	// Dim the whole screen: this is a takeover, and the carousel showing
	// through would say the app is idle while a transfer is in flight.
	Renderer::drawRect(0.f, 0.f, mSize.x(), mSize.y(), 0x000000D0);

	mBackground.render(trans);
	mTitle->render(trans);
	mStatus->render(trans);
	mFooter->render(trans);

	std::unique_lock<std::mutex> lock(mMutex);
	const bool finished = mFinished;
	const int percent = mPercent;
	lock.unlock();

	if (!finished)
	{
		if (percent >= 0)
		{
			// A bar only where there is a real number behind it. An
			// indeterminate spinner is honest; a bar at an invented
			// position is not.
			auto theme = ThemeData::getMenuTheme();
			const float w = Renderer::getScreenWidth() * 0.6f;
			const float x = Renderer::getScreenWidth() * 0.2f;
			const float y = Renderer::getScreenHeight() * 0.54f;
			const float h = Renderer::getScreenHeight() * 0.012f;
			Renderer::setMatrix(trans);
			Renderer::drawRect(x, y, w, h, (theme->Text.color & 0xFFFFFF00) | 0x40);
			Renderer::drawRect(x, y, w * (percent / 100.0f), h, theme->Text.color);
		}
		else
			mBusyAnim.render(trans);
	}
}

void GuiCloudTransfer::update(int deltaTime)
{
	GuiComponent::update(deltaTime);
	mBusyAnim.update(deltaTime);

	std::unique_lock<std::mutex> lock(mMutex);
	if (!mFinished)
		mElapsedMs += deltaTime;

	const int mins = mElapsedMs / 60000;
	const int secs = (mElapsedMs / 1000) % 60;
	char elapsed[32];
	snprintf(elapsed, sizeof(elapsed), "%d:%02d", mins, secs);

	if (mFinished)
	{
		mStatus->setText(mExit == 0
			? _("COMPLETED SUCCESSFULLY") + std::string("  -  ") + elapsed
			: _("FAILED") + std::string("  -  ") + _("SEE /var/log/cloud_sync.log"));
		mFooter->setText(_("PRESS ANY BUTTON TO CLOSE"));
	}
	else
	{
		mStatus->setText(mLine.empty() ? _("WORKING...") : mLine);
		mFooter->setText(_("ELAPSED") + std::string(" ") + elapsed
			+ "     " + _("THIS CAN TAKE A WHILE"));
	}
}

void GuiCloudTransfer::threadRun()
{
	int ret = -1;
	FILE* pipe = popen((mCommand + " 2>&1").c_str(), "r");
	if (pipe != nullptr)
	{
		char line[512];
		while (fgets(line, sizeof(line), pipe) != nullptr)
		{
			std::string clean;
			for (char c : std::string(line))
				if (c >= 32 && c < 127)
					clean += c;
			clean = Utils::String::trim(clean);

			const bool hasPercent = clean.find('%') != std::string::npos;
			const bool hasCount   = clean.find(" / ") != std::string::npos;
			if (!hasPercent && !hasCount)
				continue;

			auto eta = clean.find(", ETA ");
			if (eta != std::string::npos)
				clean = clean.substr(0, eta);
			if (clean.rfind("Transferred:", 0) == 0)
				clean = Utils::String::trim(clean.substr(12));

			int pct = -1;
			auto p = clean.find('%');
			if (p != std::string::npos && p > 0)
			{
				size_t start = p;
				while (start > 0 && isdigit((unsigned char)clean[start - 1]))
					start--;
				if (start < p)
				{
					int v = atoi(clean.substr(start, p - start).c_str());
					if (v >= 0 && v <= 100)
						pct = v;
				}
			}

			std::unique_lock<std::mutex> lock(mMutex);
			mLine = clean;
			if (pct >= 0)
				mPercent = pct;
		}
		int status = pclose(pipe);
		if (WIFEXITED(status))
			ret = WEXITSTATUS(status);
	}

	std::unique_lock<std::mutex> lock(mMutex);
	mExit = ret;
	mFinished = true;
}
