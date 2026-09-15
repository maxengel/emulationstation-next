#pragma once

#include <string>
#include "components/ComponentGrid.h"

class TextComponent;
class Window;

class MultiLineMenuEntry : public ComponentGrid
{
public:
	MultiLineMenuEntry(Window* window, const std::string& text, const std::string& substring, bool multiLine = false);

	void setColor(unsigned int color) override;
	// A dimmed entry stays dimmed: the dim is applied on every setColor,
	// which ComponentList calls each frame (ComponentListFlags::dimmed,
	// fork #182).
	void setDimmed(bool dimmed);
	bool isDimmed() const { return mDimmed; }
	void setPadding(const Vector4f padding) override;
	void onSizeChanged() override;
	void onFocusGained() override;
	void onFocusLost() override;
	
	std::string getDescription();
	void setDescription(const std::string& description);

protected:
	bool mMultiLine;
	bool mSizeChanging;
	bool mDimmed;
	// The colour the row last gave us, so setDimmed can re-apply it.
	unsigned int mColor;
	std::shared_ptr<TextComponent> mText;
	std::shared_ptr<TextComponent> mSubstring;
};
