#include "GuiComponent.h"

#include "components/NinePatchComponent.h"
#include "components/ButtonComponent.h"
#include "components/ComponentGrid.h"
#include "components/TextEditComponent.h"
#include "components/TextComponent.h"
#include <functional>

class GuiTextEditPopupKeyboard : public GuiComponent
{
public:
	GuiTextEditPopupKeyboard(Window* window, const std::string& title, const std::string& initValue,
		const std::function<void(const std::string&)>& okCallback, bool multiLine, const std::string acceptBtnText = "OK");

	bool input(InputConfig* config, Input input);
	//void update(int deltatime) override;
	void onSizeChanged();
	std::vector<HelpPrompt> getHelpPrompts() override;

	// A rule for what may be typed. Given one character, the filter returns
	// what to insert in its place -- itself, a substitute, or nothing -- and
	// may set a message the popup shows once, briefly, as a toast. It applies
	// to the on-screen keys, the shoulder-button space, and characters from a
	// physical keyboard alike, so a field with a rule cannot be typed past
	// (the HOSTNAME row: letters, digits and hyphens; fork #106).
	void setCharacterFilter(const std::function<std::string(const std::string& typed, std::string& message)>& filter) { mFilter = filter; }
	void textInput(const char* text) override;

private:
	class KeyboardButton
	{
	public:
		std::shared_ptr<ButtonComponent> button;
		const std::string key;
		const std::string shiftedKey;
		const std::string altedKey;
		const std::string altedShiftedKey;
		KeyboardButton(const std::shared_ptr<ButtonComponent> b, const std::string& k, const std::string& sk, const std::string& ak, const std::string& ask) : button(b), key(k), shiftedKey(sk), altedKey(ak), altedShiftedKey(ask) {};
	};
	
	std::shared_ptr<ButtonComponent> makeButton(const std::string& key, const std::string& shiftedKey, const std::string& altedKey, const std::string& altedShiftedKey);
	std::vector<KeyboardButton> keyboardButtons;
	
	std::shared_ptr<ButtonComponent> mShiftButton;	
	std::shared_ptr<ButtonComponent> mAltButton;

	void toggleKeyState(bool& state, std::shared_ptr<ButtonComponent>& button);
	void updateKeyboardButtons();
	void shiftKeys();
	void altKeys();

	NinePatchComponent mBackground;
	ComponentGrid mGrid;

	std::shared_ptr<TextComponent> mTitle;
	std::shared_ptr<TextEditComponent> mText;
	std::shared_ptr<ComponentGrid> mKeyboardGrid;
	
	std::function<void(const std::string&)> mOkCallback;
	std::function<std::string(const std::string&, std::string&)> mFilter;

	// Every character reaches the field through here, filtered.
	void insert(const std::string& text);

	bool mMultiLine;
	bool mShift = false;	
	bool mAlt = false;
};

