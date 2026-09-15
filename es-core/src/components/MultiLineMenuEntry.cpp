#include "MultiLineMenuEntry.h"
#include "Window.h"
#include "components/TextComponent.h"
#include "components/ComponentGrid.h"
#include "components/ComponentList.h"
#include "math/Vector2i.h"
#include "math/Vector2f.h"
#include "ThemeData.h"
#include "utils/HtmlColor.h"

#define SUBSTRING_OPACITY	192

MultiLineMenuEntry::MultiLineMenuEntry(Window* window, const std::string& text, const std::string& substring, bool multiLine) :
	ComponentGrid(window, Vector2i(1, 2))
{
	mMultiLine = multiLine;
	mSizeChanging = false;
	mDimmed = false;

	auto theme = ThemeData::getMenuTheme();
	mColor = theme->Text.color;

	mText = std::make_shared<TextComponent>(mWindow, text.c_str(), theme->Text.font, theme->Text.color);
	mText->setMultiLine(TextComponent::MultiLineType::SINGLELINE);
	mText->setVerticalAlignment(ALIGN_TOP);

	mSubstring = std::make_shared<TextComponent>(mWindow, substring.c_str(), theme->TextSmall.font, Utils::HtmlColor::applyColorOpacity(theme->Text.color, SUBSTRING_OPACITY));
	mSubstring->setVerticalAlignment(ALIGN_TOP);

	if (!multiLine)
		mSubstring->setMultiLine(TextComponent::MultiLineType::SINGLELINE);

	setEntry(mText, Vector2i(0, 0), true, true);
	setEntry(mSubstring, Vector2i(0, 1), false, true);

	layoutRows();
}

void MultiLineMenuEntry::layoutRows()
{
	float th = mText->getSize().y();

	if (mSubstring->getText().empty())
	{
		setRowHeight(0, th);
		setRowHeight(1, 0);

		setSize(Vector2f(mSize.x(), th));
	}
	else
	{
		float sh = mSubstring->getSize().y();
		float h = th + sh;

		setRowHeightPerc(0, (th * 0.9) / h);
		setRowHeightPerc(1, (sh * 1.1) / h);

		setSize(Vector2f(mSize.x(), h));
	}
}

void MultiLineMenuEntry::setColor(unsigned int color)
{
	mColor = color;
	const unsigned int shown = mDimmed ? ComponentListFlags::dimmed(color) : color;
	mText->setColor(shown);
	mSubstring->setColor(Utils::HtmlColor::applyColorOpacity(shown, SUBSTRING_OPACITY));
}

void MultiLineMenuEntry::setDimmed(bool dimmed)
{
	mDimmed = dimmed;
	setColor(mColor);
}

// A ComponentGrid draws nothing of its own, so padding set on the entry has
// to reach the two text components inside it. Without this, an entry placed
// in a non-selectable row sits flush against the left edge while selectable
// rows and group headers are inset by ComponentList (which only offsets
// selectable rows -- see TOTAL_HORIZONTAL_PADDING_PX there).
void MultiLineMenuEntry::setPadding(const Vector4f padding)
{
	GuiComponent::setPadding(padding);
	mText->setPadding(padding);
	mSubstring->setPadding(padding);
}

std::string MultiLineMenuEntry::getDescription()
{
	return mSubstring->getText();
}

void MultiLineMenuEntry::setDescription(const std::string& description)
{
	mSubstring->setText(description);
	if (mMultiLine)
		onSizeChanged();
	else
		layoutRows();
}

void MultiLineMenuEntry::onSizeChanged()
{		
	ComponentGrid::onSizeChanged();

	if (mMultiLine && mSubstring && mSize.x() > 0 && !mSizeChanging)
	{
		mSizeChanging = true;

		mText->setSize(mSize.x(), 0);
		mSubstring->setSize(mSize.x(), 0);

		layoutRows();

		mSizeChanging = false;
	}
}

void MultiLineMenuEntry::onFocusGained()
{
	ComponentGrid::onFocusGained();

	if (!mMultiLine && mSubstring)
		mSubstring->setAutoScroll(TextComponent::AutoScrollType::HORIZONTAL);

}

void MultiLineMenuEntry::onFocusLost()
{
	ComponentGrid::onFocusLost();

	if (!mMultiLine && mSubstring)
		mSubstring->setAutoScroll(TextComponent::AutoScrollType::NONE);
}
