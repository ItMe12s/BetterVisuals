#include <Geode/Geode.hpp>
#include <Geode/modify/EditorPauseLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>

using namespace geode::prelude;

class PopupModSettings : public CCLayer {
public:
    void ShowPopup(CCObject*) {
        openSettingsPopup(Mod::get(), true);
    }
};

void makeSettingsButton(CCLayer* parent, char const* menuId) {
    auto menu = parent->getChildByID(menuId);
    auto spr = CCSprite::create("Button.png"_spr);
    auto size = spr->getContentSize();
    auto scale = 40.f / size.width;
    spr->setScale(scale);

    auto btn = CCMenuItemSpriteExtra::create(spr, parent, menu_selector(PopupModSettings::ShowPopup));
    btn->setID("settings-button"_spr);
    menu->addChild(btn);
    menu->updateLayout();
}

class $modify(PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        if (Mod::get()->getSettingValue<bool>("pause-menu-button")) {
            makeSettingsButton(this, "left-button-menu");
        }
    }
};

class $modify(EditorPauseLayer) {
    bool init(LevelEditorLayer* po) {
        if (!EditorPauseLayer::init(po)) return false;
        if (Mod::get()->getSettingValue<bool>("editor-pause-button")) {
            makeSettingsButton(this, "guidelines-menu");
        }
        return true;
    }
};