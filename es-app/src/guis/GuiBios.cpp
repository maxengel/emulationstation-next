#include "guis/GuiBios.h"

#include "ApiSystem.h"
#include "components/OptionListComponent.h"
#include "guis/GuiSettings.h"
#include "views/ViewController.h"
#include "SystemData.h"
#include "LocaleES.h"
#include "components/ComponentGrid.h"
#include "components/MultiLineMenuEntry.h"
#include "components/ComponentTab.h"
#include "components/ButtonComponent.h"
#include "GuiLoading.h"
#include "guis/GuiMsgBox.h"
#include "SystemConf.h"

#include <cstring>

#define WINDOW_WIDTH (float)Math::max((int)Renderer::getScreenHeight(), (int)(Renderer::getScreenWidth() * 0.65f))

void GuiBios::show(Window* window)
{
	window->pushGui(new GuiLoading<std::vector<BiosSystem>>(window, _("PLEASE WAIT"),
		[](auto gui) { return ApiSystem::getInstance()->getBiosInformations(); },
		[window](std::vector<BiosSystem> ra) 
	{ 
		if (ra.size() == 0)
			window->pushGui(new GuiMsgBox(window, _("NO MISSING BIOS FILES"), _("OK")));
		else
			window->pushGui(new GuiBios(window, ra)); 
	}));
}

GuiBios::GuiBios(Window* window, const std::vector<BiosSystem> bioses)
	: GuiComponent(window), mGrid(window, Vector2i(1, 4)), mBackground(window, ":/frame.png"), mTabFilter(0)
{
	mBios = bioses;

	addChild(&mBackground);
	addChild(&mGrid);

	// Form background
	auto theme = ThemeData::getMenuTheme();
	mBackground.setImagePath(theme->Background.path);
	mBackground.setEdgeColor(theme->Background.color);
	mBackground.setCenterColor(theme->Background.centerColor);
	mBackground.setCornerSize(theme->Background.cornerSize);
	mBackground.setPostProcessShader(theme->Background.menuShader);

	// Title
	mTitle = std::make_shared<TextComponent>(mWindow, _("MISSING BIOS CHECK"), theme->Title.font, theme->Title.color, ALIGN_CENTER);
	mGrid.setEntry(mTitle, Vector2i(0, 0), false, true);

	// Tabs
	mTabs = std::make_shared<ComponentTab>(mWindow);
	// Named for what the filter is, not for a concept the player does not
	// have. "Installed systems" meant "systems EmulationStation has loaded",
	// which it does only when a system has games -- so the first tab is the
	// BIOS gaps for the games on this device, and the maintainer could not
	// tell that from the label.
	mTabs->addTab(_("SYSTEMS WITH GAMES"));
	mTabs->addTab(_("ALL SYSTEMS"));

	mTabs->setCursorChangedCallback([&](const CursorState& /*state*/)
		{			
			if (mTabFilter != mTabs->getCursorIndex())
			{
				mTabFilter = mTabs->getCursorIndex();
				loadList();
			}
		});

	mGrid.setEntry(mTabs, Vector2i(0, 1), false, true);

	// Entries
	mList = std::make_shared<ComponentList>(mWindow);
	mList->setUpdateType(ComponentListFlags::UpdateType::UPDATE_ALWAYS);

	mGrid.setEntry(mList, Vector2i(0, 2), true, true);

	// Buttons
	std::vector<std::shared_ptr<ButtonComponent>> buttons;
	buttons.push_back(std::make_shared<ButtonComponent>(mWindow, _("REFRESH"), _("refresh"), [&] { refresh(); }));
	buttons.push_back(std::make_shared<ButtonComponent>(mWindow, _("BACK"), _("BACK"), [this] { delete this; }));

	mButtonGrid = makeButtonGrid(mWindow, buttons);
	mGrid.setEntry(mButtonGrid, Vector2i(0, 3), true, false);

	mGrid.setUnhandledInputCallback([this](InputConfig* config, Input input) -> bool
		{
			if (config->isMappedLike("down", input)) { mGrid.setCursorTo(mList); mList->setCursorIndex(0); return true; }
			if (config->isMappedLike("up", input)) { mList->setCursorIndex(mList->size() - 1); mGrid.moveCursor(Vector2i(0, 1)); return true; }
			return false;
		});

	centerWindow();
	loadList();
}

void GuiBios::onSizeChanged()
{
	GuiComponent::onSizeChanged();

	mBackground.fitTo(mSize, Vector3f::Zero(), Vector2f(-32, -32));

	mGrid.setSize(mSize);

	const float titleHeight = mTitle->getFont()->getLetterHeight();
	const float titleSubtitleSpacing = mSize.y() * 0.03f;

	mGrid.setRowHeight(0, titleHeight + titleSubtitleSpacing  + (Renderer::getScreenHeight() * 0.05f));

	if (mTabs->size() == 0)
		mGrid.setRowHeight(1, 0.00001f);
	else
		mGrid.setRowHeight(1, titleHeight * 2);

	mGrid.setRowHeight(3, mButtonGrid->getSize().y());	
}

void GuiBios::refresh()
{
	mWindow->pushGui(new GuiLoading<std::vector<BiosSystem>>(mWindow, _("PLEASE WAIT"),
		[](auto gui) { return ApiSystem::getInstance()->getBiosInformations(); },
		[this](std::vector<BiosSystem> ra) { mBios = ra; loadList(); }));
}

void GuiBios::loadList()
{
#define INVALID_ICON _U("\uF071")
#define MISSING_ICON _U("\uF127")
	
	auto theme = ThemeData::getMenuTheme();

	int idx = mList->getCursorIndex();
	mList->clear();

	// Whether anything survived the tab's filter, which is not the same
	// question as whether the scan found anything. INSTALLED SYSTEMS hides a
	// system EmulationStation has not loaded, and it loads a system only when
	// that system has games -- so a device holding BIOS gaps for nds, psx and
	// segacd, with games for none of them, showed a completely empty page.
	// Empty is the correct answer there; a blank page is not, because it is
	// what a broken screen looks like too.
	bool shown = false;

	for (auto systemBiosData : mBios)
	{
		ComponentListRow row;

		std::string name = Utils::String::proper(systemBiosData.name); // systemBiosData.name;

		bool isKnownSystem = false;

		for (auto sys : SystemData::sSystemVector)
		{
			if (sys->getName() == systemBiosData.name)
			{
				isKnownSystem = true;
				name = sys->getFullName();
			}
		}

		if (!isKnownSystem && mTabFilter == 0)
			continue;

		shown = true;
		mList->addGroup(name, true);

		for (auto biosFile : systemBiosData.bios)
		{
			auto theme = ThemeData::getMenuTheme();

			ComponentListRow row;

			auto icon = std::make_shared<TextComponent>(mWindow);
			icon->setText(biosFile.status == "INVALID" ? INVALID_ICON : MISSING_ICON);
			icon->setColor(theme->Text.color);
			icon->setFont(theme->Text.font);
			icon->setSize(theme->Text.font->getLetterHeight() * 1.5f, 0);
			row.addElement(icon, false);

			auto spacer = std::make_shared<GuiComponent>(mWindow);
			spacer->setSize(14, 0);
			row.addElement(spacer, false);

			std::string status = _(biosFile.status.c_str()) + std::string(biosFile.md5.empty() || biosFile.md5 == "-" ? "" : " - MD5: " + biosFile.md5);

			auto line = std::make_shared<MultiLineMenuEntry>(mWindow, biosFile.path, status);
			row.addElement(line, true);

			mList->addRow(row);
		}
	}

	if (!shown)
	{
		std::shared_ptr<Font> font = theme->Text.font;
		unsigned int color = theme->Text.color;

		// Two different answers, because they lead somewhere different. With
		// nothing found at all there is nothing to do. With findings that this
		// tab filtered away, the next move is the other tab -- and saying so
		// is the whole difference between a finished check and a broken one.
		auto text = std::make_shared<TextComponent>(mWindow,
			mBios.size() == 0 ? _("NO MISSING BIOS")
			: mTabFilter == 0 ? _("NO MISSING BIOS FOR THE SYSTEMS YOU HAVE GAMES FOR. SEE ALL SYSTEMS FOR THE REST.")
			: _("NO MISSING BIOS"),
			font, color);
		if (EsLocale::isRTL())
			text->setHorizontalAlignment(Alignment::ALIGN_RIGHT);

		// Wrap, or the sentence above runs off the right edge of a 640-wide
		// panel. A TextComponent measures itself as one line when it is
		// built; the list then keeps that height while widening it, so it
		// never re-wraps. Height 0 hands the height back to the text, which
		// wraps at the width it is given and grows the row to fit. The list
		// may not be sized yet on the first pass, so fall back to the
		// window's own width rule.
		float wrapWidth = mList->getSize().x();
		if (wrapWidth <= 0)
			wrapWidth = Renderer::ScreenSettings::fullScreenMenus() ? Renderer::getScreenWidth() : WINDOW_WIDTH;
		text->setMultiLine(MultiLineType::MULTILINE);
		text->setSize(wrapWidth * 0.94f, 0);

		ComponentListRow row;
		row.addElement(text, true);
		mList->addRow(row);
	}

	centerWindow();	
}

void GuiBios::centerWindow()
{
	if (Renderer::ScreenSettings::fullScreenMenus())
		setSize(Renderer::getScreenWidth(), Renderer::getScreenHeight());
	else
		setSize(WINDOW_WIDTH, Renderer::getScreenHeight() * 0.875f);

	setPosition((Renderer::getScreenWidth() - getSize().x()) / 2, (Renderer::getScreenHeight() - getSize().y()) / 2);
}

bool GuiBios::input(InputConfig* config, Input input)
{
	if (GuiComponent::input(config, input))
		return true;

	if (input.value != 0 && config->isMappedTo(BUTTON_BACK, input))
	{
		delete this;
		return true;
	}

	if (config->isMappedTo("start", input) && input.value != 0)
	{
		refresh();
		return true;
	}

	if (mTabs->input(config, input))
		return true;

	return false;
}

std::vector<HelpPrompt> GuiBios::getHelpPrompts()
{
	std::vector<HelpPrompt> prompts; // = mMenu.getHelpPrompts();
	prompts.push_back(HelpPrompt(BUTTON_BACK, _("BACK")));
	prompts.push_back(HelpPrompt("start", _("REFRESH")));
	return prompts;
}
