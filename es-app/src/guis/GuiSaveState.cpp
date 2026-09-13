#include "GuiSaveState.h"
#include "SystemData.h"
#include "FileData.h"
#include "utils/StringUtil.h"
#include "utils/TimeUtil.h"
#include "utils/FileSystemUtil.h"
#include "ApiSystem.h"
#include "HelpStyle.h"
#include "SystemConf.h"
#include "guis/GuiMsgBox.h"
#include "SaveStateRepository.h"
#include "ThreadedCloudSync.h"

// 0.55 of the screen: the sheet has to hold a tile whose label is two
// lines of the small font over a thumbnail still worth looking at. At 0.40
// a 640x480 panel left a 92 px grid row, one line of label, and START NEW
// GAME / START NEW AUTO SAVE ending in "..." (#27); at 0.50 two lines left
// the thumbnail 74 px tall there.
#define WINDOW_HEIGHT Renderer::getScreenHeight() * 0.55f

static int slots = 6; // 5;

GuiSaveState::GuiSaveState(Window* window, FileData* game, const std::function<void(SaveState* state)>& callback) :
	GuiComponent(window), mGrid(nullptr), mBackground(window, ":/frame.png"),
	mLayout(window, Vector2i(3, 5)), mTitle(nullptr)
{
	mGame = game;
	mRepository = game->getSourceFileData()->getSystem()->getSaveStateRepository();
	mRunCallback = callback;

	// Form background
	auto theme = ThemeData::getMenuTheme();
	mBackground.setImagePath(theme->Background.path); // ":/frame.png"
	mBackground.setEdgeColor(theme->Background.color);
	mBackground.setCenterColor(theme->Background.centerColor);
	mBackground.setCornerSize(theme->Background.cornerSize);
	mBackground.setPostProcessShader(theme->Background.menuShader);

	mTitle = std::make_shared<TextComponent>(mWindow, _("SAVE STATE MANAGER"), theme->Title.font, theme->Title.color, ALIGN_CENTER);
	mLayout.setEntry(mTitle, Vector2i(1, 1), false, true);

	mGrid = std::make_shared<ImageGridComponent<SaveStateItem>>(mWindow);
	mLayout.setEntry(mGrid, Vector2i(1, 3), true, true);

	addChild(&mBackground);
	addChild(&mLayout);
	
	float cellProportion = 1.77;
	float screenProportion = (float)Renderer::getScreenWidth() / (float)Renderer::getScreenHeight();

	float sh = (float)Math::min(Renderer::getScreenHeight(), Renderer::getScreenWidth());
	sh = (float) theme->TextSmall.font->getSize() / sh;

	// The label's share of a tile is whatever two lines of the tile's font
	// are of the tile's height, never less than the 0.30 it was nor more than half (#27). The
	// tile is the grid's one row: the sheet less its spacing, title and help
	// rows, laid out by onSizeChanged with the same arithmetic. TextComponent
	// wraps on its own once its height clears 1.8 lines, so two full lines
	// make START NEW GAME, START NEW AUTO SAVE and a slot's number-and-date
	// wrap where they used to end in "...". The strings themselves are
	// untouched: they are msgids in every locale.
	// Font::getHeight is the tallest glyph rasterised SO FAR, so measured
	// before the labels have drawn it under-reports: at 640x480 the share
	// computed from it fell to the 0.30 floor while the renderer, measuring
	// later with more glyphs loaded, found the area under its two-line
	// threshold and ellipsised every label (878ec8863b, 2026-09-13).
	// Rasterise the printable range first, so this height is the one the
	// labels are laid out with; and the tile text is told to wrap outright
	// (<multiLine>true</multiLine>) rather than left to that threshold.
	std::string ascii;
	for (int c = 32; c < 127; c++)
		ascii.push_back((char)c);
	theme->TextSmall.font->sizeText(ascii);

	// The widest line a tile shows is a slot's date and time. A tile is the
	// grid's width over the columns it lays out (the same arithmetic as
	// ImageGridComponent::calcGridDimension: the sheet less its two 0.01
	// side columns, less a margin between columns), and on a 640x480 panel
	// that is about 152 px while the date in the small font is about 150:
	// the labels of neighbouring tiles ran into each other ("21:4709/12/
	// 2026", 02f368914e). Where the date would take more than 0.86 of the
	// tile, the label font shrinks so it fits with a gap either side; the
	// two-line height below is then measured on that smaller font.
	const int columns = (int)(slots * screenProportion / cellProportion);
	const float marginX = 0.01f * (float)Renderer::getScreenWidth();
	const float tileWidth = ((float)Renderer::getScreenWidth() * 0.98f - marginX * (columns - 1)) / (float)Math::max(1, columns);
	std::shared_ptr<Font> labelFont = theme->TextSmall.font;
	{
		const std::string widest = Utils::Time::DateTime::now().toLocalTimeString();
		const float widestPx = labelFont->sizeText(widest).x();
		if (widestPx > tileWidth * 0.86f && widestPx > 0)
		{
			const int shrunk = (int)((float)labelFont->getSize() * tileWidth * 0.86f / widestPx);
			labelFont = Font::get(Math::max(shrunk, 1), labelFont->getPath());
			labelFont->sizeText(ascii);
			sh = (float)labelFont->getSize() / (float)Math::min(Renderer::getScreenHeight(), Renderer::getScreenWidth());
		}
	}
	const float sheetHeight = WINDOW_HEIGHT;
	const float titlePerc = theme->Title.font->getHeight(2.0f) / sheetHeight;
	const float gridHeight = sheetHeight * (1.0f - 0.02f - titlePerc - 0.02f - helpRowPerc(sheetHeight));
	const float twoLines = 2.0f * labelFont->getHeight(1.5f) + 4.0f;
	float labelPerc = gridHeight > 0 ? twoLines / gridHeight : 0.30f;
	labelPerc = Math::max(0.30f, Math::min(0.50f, labelPerc));

	std::string xml =
		"<theme defaultView=\"Tiles\">"
		"<formatVersion>7</formatVersion>"
		"<view name = \"grid\">"
		"<imagegrid name=\"gamegrid\">"
		"  <margin>0.01 0.02</margin>"
		"  <padding>0 0</padding>"
		"  <scrollDirection>horizontal</scrollDirection>"
		"  <autoLayout>" + std::to_string(slots * screenProportion / cellProportion) +" 1</autoLayout>"
		"  <autoLayoutSelectedZoom>1</autoLayoutSelectedZoom>"
		"  <animateSelection>false</animateSelection>"
		"  <centerSelection>false</centerSelection>"		
		"</imagegrid>"		
		"<gridtile name=\"default\">"
		"  <backgroundColor>FFFFFF00</backgroundColor>"
		"  <padding>8 8</padding>"
		"  <imageColor>FFFFFFFF</imageColor>"
		"</gridtile>"		
		"<gridtile name=\"selected\">"
		"  <backgroundColor>" + Utils::String::toHexString(theme->Text.selectorColor) + "</backgroundColor>"
		"</gridtile>"
		"<text name=\"gridtile\">"
		"  <color>" + Utils::String::toHexString(theme->Text.color) + "</color>"
		"  <backgroundColor>00000000</backgroundColor>"
		"  <fontPath>" + theme->Text.font->getPath() +"</fontPath>"
		"  <fontSize>" + std::to_string(sh) + "</fontSize>"
		"  <alignment>center</alignment>"
		"  <singleLineScroll>false</singleLineScroll>"
		"  <multiLine>true</multiLine>"
		"  <size>1 " + std::to_string(labelPerc) + "</size>"
		"</text>"
		"<text name=\"gridtile:selected\">"
		"  <color>" + Utils::String::toHexString(theme->Text.selectedColor) + "</color>"
		"</text>"
		"<image name=\"gridtile.image\">"
		"  <linearSmooth>true</linearSmooth>"		
		"  <color>D0D0D0D0</color>"
		"  <roundCorners>0.02</roundCorners>"		
		"</image>"
		"<image name=\"gridtile.image:selected\">"
		"  <color>FFFFFFFF</color>"
		"</image>"
		"</view>"
		"</theme>";

	mTheme = std::shared_ptr<ThemeData>(new ThemeData());
	std::map<std::string, std::string> emptyMap;
	mTheme->loadFile("imageviewer", emptyMap, xml, false);

	//mGrid->setSize(Renderer::getScreenWidth(), Renderer::getScreenHeight());
	mGrid->applyTheme(mTheme, "grid", "gamegrid", 0);
	mGrid->setCursorChangedCallback([&](const CursorState& /*state*/) { updateHelpPrompts(); });

	loadGrid();
	centerWindow();
}

void GuiSaveState::loadGrid()
{
	mGrid->clear();
	mGrid->onSizeChanged(); // To Rebuild tiles

	bool supportsIncrementalSaveStates = SystemConf::getIncrementalSaveStates();
	bool incrementalSaveStates = supportsIncrementalSaveStates && mRepository->supportsIncrementalSaveStates();

	auto states = mRepository->getSaveStates(mGame);
	
	std::sort(states.begin(), states.end(), [&, supportsIncrementalSaveStates, incrementalSaveStates](const SaveState* file1, const SaveState* file2)
		{ 
			// Show active emulator first
			if (file1->config != nullptr && file2->config != nullptr && !file1->config->equals(file2->config))
				return file1->config->isActiveConfig(mGame);
			
			if (supportsIncrementalSaveStates && file1->config != nullptr ? file1->config->incremental : incrementalSaveStates)
				return file1->creationDate >= file2->creationDate;

			return file1->slot < file2->slot; 
		});


	mGrid->add(_("START NEW GAME"), ":/freeslot.svg", SaveStateItem(mRepository->getDefaultNewGameSaveState()));

	if (mRepository->supportsAutoSave() && mGame->getCurrentGameSetting("autosave") == "1")
	{
		auto autoSave = std::find_if(states.cbegin(), states.cend(), [](SaveState* x) { return x->slot == -1; });
		if (autoSave == states.cend())
			mGrid->add(_("START NEW AUTO SAVE"), ":/freeslot.svg", SaveStateItem(mRepository->getDefaultAutoSaveSaveState()));
	}

	for (auto item : states)
	{
		std::string coreinfo;
		
		if (item->config != nullptr && !item->config->emulator.empty() && !item->config->isActiveConfig(mGame))
		{
			if (item->config->core.empty() || item->config->emulator == item->config->core)
				coreinfo = std::string("\r\n") + item->config->emulator;
			else
				coreinfo = std::string("\r\n") + item->config->emulator + ": " + item->config->core;
		}

		if (item->slot == -1)
			mGrid->add(_("AUTO SAVE") + std::string("\r\n") + item->creationDate.toLocalTimeString() + coreinfo, item->getScreenShot(), SaveStateItem(item));
		else if (supportsIncrementalSaveStates && item->config != nullptr ? item->config->incremental : incrementalSaveStates)
			mGrid->add(item->creationDate.toLocalTimeString() + coreinfo, item->getScreenShot(), SaveStateItem(item));
		else 
			mGrid->add(_("SLOT") + std::string(" ") + std::to_string(item->slot) + std::string("\r\n") + item->creationDate.toLocalTimeString() + coreinfo, item->getScreenShot(), SaveStateItem(item));
	}

	// The help bar follows the cursor, and a rebuild moves the cursor
	// without a cursor event: after the last slot was deleted the bar still
	// offered DELETE and COPY TO FREE SLOT over START NEW GAME (#93). Read
	// the prompts of whatever is under the cursor now. A no-op before the
	// page is on screen (updateHelpPrompts acts only on the top page), so
	// the constructor's call costs nothing.
	updateHelpPrompts();
}

// The help row's share of a sheet of the given height -- shared by the
// layout and by the label arithmetic in the constructor, so the two agree.
float GuiSaveState::helpRowPerc(float sheetHeight)
{
	float helpSize = 0.02;

	if (Settings::getInstance()->getBool("ShowHelpPrompts"))
	{
		HelpStyle help;
		if (mTheme != nullptr)
			help.applyTheme(mTheme, "system");

		const float height = Math::round(help.font->getLetterHeight() * 1.25f);

		float helpBottom = help.position.y() + (height * mOrigin.y());
		float helpBottomSpace = 0; // Renderer::getScreenHeight() - helpBottom;

		float helpTop = help.position.y() - (height * mOrigin.y()); // +height / 2;
		
		helpSize = helpTop;
		helpSize = Renderer::getScreenHeight() - helpSize + helpBottomSpace;
		helpSize = helpSize / sheetHeight + 0.06;
	}

	return helpSize;
}

void GuiSaveState::onSizeChanged()
{	
	GuiComponent::onSizeChanged();

	float helpSize = helpRowPerc(mSize.y());

	mBackground.fitTo(mSize, Vector3f::Zero(), Vector2f(-32, -32));

	mLayout.setColWidthPerc(0, 0.01);
	mLayout.setColWidthPerc(2, 0.01);

	mLayout.setRowHeightPerc(0, 0.02);

	if (mTitle != nullptr &&  mTitle->getFont() != nullptr)
		mLayout.setRowHeightPerc(1, mTitle->getFont()->getHeight(2.0f) / mSize.y());

	mLayout.setRowHeightPerc(2, 0.02);
	mLayout.setRowHeightPerc(4, helpSize );

	mLayout.setSize(mSize);
}

void GuiSaveState::render(const Transform4x4f& parentTrans)
{
	GuiComponent::render(parentTrans);

	// The layout keeps its bottom row free for the help prompts
	// (onSizeChanged), but with full-screen menus on -- every handheld
	// panel -- Window draws no help while a second page is open, so on a
	// 640x480 panel this page never showed BACK / LAUNCH / DELETE / COPY TO
	// FREE SLOT at all (2026-09-13, found framing #27). Draw them here, as
	// ViewController does when it is the top page, into the row kept for
	// them; Window then skips its own draw for this frame.
	if (mWindow->peekGui() == this && Renderer::ScreenSettings::fullScreenMenus())
		mWindow->renderHelpPromptsEarly(parentTrans);
}

void GuiSaveState::centerWindow()
{
	setSize(Renderer::getScreenWidth(), WINDOW_HEIGHT);
	animateTo(
		Vector2f((Renderer::getScreenWidth() - getSize().x()) / 2, Renderer::getScreenHeight()),
		Vector2f((Renderer::getScreenWidth() - getSize().x()) / 2, Renderer::getScreenHeight() - WINDOW_HEIGHT),
		AnimateFlags::OPACITY | AnimateFlags::POSITION);
}

bool GuiSaveState::input(InputConfig* config, Input input)
{
	if (input.value != 0 && (config->isMappedTo(BUTTON_BACK, input) || config->isMappedTo("l3", input)))
	{
		delete this;
		return true;
	}

	if (input.value != 0 && config->isMappedTo(BUTTON_OK, input))
	{
		if (mGrid->size())
		{
			const SaveStateItem& item = mGrid->getSelected();
			mRunCallback(item.saveState);
		}

		delete this;
		return true;
	}

	if (input.value != 0 && config->isMappedTo("y", input))
	{
		// Every writer of the saves tree is gated by the transfer lock
		// (D-CLOUD-053): a deletion and the renumber it triggers, landing
		// while a sync is reading that tree, is the race the maintainer
		// named -- exit a game, the backup starts, delete a save under it.
		// Refused, never waited for (nobody waits, #22 R6), in the words the
		// launch gate uses for the same state.
		if (ThreadedCloudSync::isRunning())
		{
			mWindow->pushGui(new GuiMsgBox(mWindow,
				_("YOUR SAVES ARE SYNCING WITH THE CLOUD.\n\nWAIT FOR IT TO FINISH BEFORE DELETING A SAVE STATE - THE NOTIFICATION AT THE TOP SAYS WHEN IT IS DONE.")));
			return true;
		}

		if (mGrid->size())
		{
			mWindow->pushGui(new GuiMsgBox(mWindow, _("ARE YOU SURE YOU WANT TO DELETE THIS ITEM?"), _("YES"), 
				[this]
				{
					
					const SaveStateItem& toDelete = mGrid->getSelected();
					auto conf = toDelete.saveState->config;

					// The grid also holds the START NEW GAME / START NEW AUTO SAVE
					// placeholders, for which remove() below is a no-op; only a real
					// file is recorded and rescanned.
					bool recordDeletion = Utils::FileSystem::exists("/usr/bin/cloud_capture")
						&& toDelete.saveState->isSlotValid()
						&& !toDelete.saveState->fileName.empty();

					if (recordDeletion)
					{
						// Record the deletion before the file goes (#21 R3, D-CLOUD-053): the
						// next pass then propagates a decided deletion instead of asking about
						// an absence it cannot explain (D-CLOUD-037).
						std::string retire = std::string("/usr/bin/cloud_capture --retire ")
							+ Utils::String::shellQuote(toDelete.saveState->fileName);
						if (!toDelete.saveState->getScreenShot().empty())
							retire += " " + Utils::String::shellQuote(toDelete.saveState->getScreenShot());
						// The deletion proceeds either way (the player asked for it); a
						// retire that could not record is logged, as launchGame logs a
						// capture that could not, so the absence has a trace somewhere.
						int retireCode = ApiSystem::executeScriptLegacy(retire, nullptr).second;
						if (retireCode != 0)
							LOG(LogWarning) << "cloud_capture --retire exited " << retireCode << " -- see /var/log/cloud_sync.log and /storage/.cache/cloud_sync/capture-failures";
					}

					toDelete.saveState->remove();

					SaveStateRepository::renumberSlots(mGame, conf);

					if (recordDeletion)
					{
						// The renumber moved every slot above the deleted one; re-key now
						// rather than at the next exit. --rescan carries no provenance, which
						// is the point: no game ran, so there is no frozen emulator or core to
						// pass and getEmulator()/getCore() must not be used in its place.
						FileData* game = mGame->getSourceFileData();
						int rescanCode = ApiSystem::executeScriptLegacy(std::string("/usr/bin/cloud_capture --rescan --system ")
							+ Utils::String::shellQuote(game->getSystem()->getName())
							+ " --rom " + Utils::String::shellQuote(game->getPath()), nullptr).second;
						if (rescanCode != 0)
							LOG(LogWarning) << "cloud_capture --rescan exited " << rescanCode << " -- see /var/log/cloud_sync.log and /storage/.cache/cloud_sync/capture-failures";
					}

					mRepository->refresh();

					loadGrid();
				}, 
				_("NO"), nullptr));
		}

		return true;
	}
	
	if (input.value != 0 && config->isMappedTo("x", input))
	{
		if (mGrid->size())
		{
			const SaveStateItem& toCopy = mGrid->getSelected();
			
			int slot = mRepository->getNextFreeSlot(mGame, toCopy.saveState->config);
			if (slot >= 0)
			{				
				if (toCopy.saveState->copyToSlot(slot))
				{
					mRepository->refresh();
					loadGrid();
				}
			}
		}

		return true;
	}
	
	return GuiComponent::input(config, input);
}

std::vector<HelpPrompt> GuiSaveState::getHelpPrompts()
{
	std::vector<HelpPrompt> prompts;
	prompts.push_back(HelpPrompt(BUTTON_BACK, _("BACK"), [&] { delete this; }));
	prompts.push_back(HelpPrompt(BUTTON_OK, _("LAUNCH")));

	if (mGrid->size())
	{
		const SaveStateItem& item = mGrid->getSelected();
		if (!item.saveState->fileName.empty())
		{
			prompts.push_back(HelpPrompt("y", _("DELETE")));
			prompts.push_back(HelpPrompt("x", _("COPY TO FREE SLOT")));
		}
	}	

	return prompts;
}

bool GuiSaveState::hitTest(int x, int y, Transform4x4f& parentTransform, std::vector<GuiComponent*>* pResult)
{
	if (pResult) pResult->push_back(this); // Always return this as it's a fake fullscreen, so we always have click events
	GuiComponent::hitTest(x, y, parentTransform, pResult);
	return true;
}

bool GuiSaveState::onMouseClick(int button, bool pressed, int x, int y)
{
	if (pressed && button == 1 && !mBackground.isMouseOver())
	{
		delete this;
		return true;
	}

	return (button == 1);
}
