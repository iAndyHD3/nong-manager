#include <matdash.hpp>

// defines add_hook to use minhook
#include <matdash/minhook.hpp>

// lets you use mod_main
#include <matdash/boilerplate.hpp>

#include <fmt/format.h>
#include <gd.h>
#include <filesystem>
#include "../libs/hps/hps.h"
#include <map>
#include <iostream>
#include <fstream>
#include <charconv>
#include <Windows.h>
#include <shellapi.h>


using namespace gd;
using namespace cocos2d;
namespace fs = std::filesystem;

//#define USE_WIN32_CONSOLE
#define MEMBERBYOFFSET(type, class, offset) *reinterpret_cast<type*>(reinterpret_cast<uintptr_t>(class) + offset)
#define MBO MEMBERBYOFFSET


#define public_cast(value, member) [](auto* v) { \
	class FriendClass__; \
	using T = std::remove_pointer<decltype(v)>::type; \
	class FriendeeClass__: public T { \
	protected: \
		friend FriendClass__; \
	}; \
	class FriendClass__ { \
	public: \
		auto& get(FriendeeClass__* v) { return v->member; } \
	} c; \
	return c.get(reinterpret_cast<FriendeeClass__*>(v)); \
}(value)

template <typename T, typename U>
T union_cast(U value)
{
	union {
		T a;
		U b;
	} u;
	u.b = value;
	return u.a;
}

void set_clipboard_text(std::string_view text)
{
	if (!OpenClipboard(NULL)) return;
	if (!EmptyClipboard()) return;
	const auto len = text.size();
	auto mem = GlobalAlloc(GMEM_MOVEABLE, len + 1);
	memcpy(GlobalLock(mem), text.data(), len + 1);
	GlobalUnlock(mem);
	SetClipboardData(CF_TEXT, mem);
	CloseClipboard();
}

std::map<int, std::string> _paths;
std::vector<std::string> _directories;

std::string getSaveFilePath()
{
	//just do this once
	static std::string filePath {""};
	if(filePath.empty()) {
		filePath = fmt::format("{}NONGManager.dat", CCFileUtils::sharedFileUtils()->getWritablePath());
	}
	return filePath;
}

std::string_view getWritablePath() { return CCFileUtils::sharedFileUtils()->getWritablePath(); }

std::vector<std::string> getDirectories(std::string_view dirPath)
{
	std::vector<std::string> directories;
	for (const auto& dirEntry : std::filesystem::directory_iterator(dirPath))
	{
		if (dirEntry.is_directory()) {
			directories.emplace_back(dirEntry.path().filename().string());
		}
	}
	return directories;
}

//done here because less chance of hook conflict than with something like loadAssets
const char* LoadingLayer_getLoadingString(void* self)
{
	std::string path = getSaveFilePath();
	
	std::ifstream in(path);
	if(in.good())
	{
		std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		_paths = hps::from_string<std::map<int, std::string>>(contents);
	}
	_directories = getDirectories(getWritablePath());
	return matdash::orig<&LoadingLayer_getLoadingString>(self);
}


std::string MusicDownloadManager_pathForSong(MusicDownloadManager* self, int songID)
{
	if(_paths.count(songID) == 0)
		return matdash::orig<&MusicDownloadManager_pathForSong>(self, songID);

	std::string path = fmt::format("{}{}\\{}.mp3", getWritablePath(), _paths[songID], songID);
	if(std::filesystem::exists(path))
	{
		fmt::print("custom file path: {}", path);
		return path;
	}
	
	return matdash::orig<&MusicDownloadManager_pathForSong>(self, songID);
}

struct CallBacks
{
	static std::vector<std::string_view> directoriesSongExists;
	void onArrow(CCObject* sender)
	{
		// _paths -> std::map<int, std::string>
		// _directories -> std::vector<std::string>
	
		// if right arrow was clicked go up
		bool up = sender->getTag() == -1;
	
		auto layer = (CCLayer*)((CCNode*)this)->getChildren()->objectAtIndex(0);
		int songID = layer->getTag();
		auto it = _paths.find(songID);
		std::string dirName = it != _paths.end() ? it->second : "";
	
		auto label = (CCLabelBMFont*)layer->getChildByTag(songID);
		fmt::print("song id callback: {}\n", songID);
	
		// find dirName in _directories
		auto it_dir = std::find(directoriesSongExists.begin(), directoriesSongExists.end(), dirName);
		std::string next_dir {};
		if (it_dir == directoriesSongExists.end()) {
			next_dir = directoriesSongExists[0];
		}
		else
		{
			// get its index
			auto idx = std::distance(directoriesSongExists.begin(), it_dir);
	
			// get the next directory
			auto next_idx = up ? (idx == 0 ? directoriesSongExists.size() - 1 : idx - 1) : (idx == directoriesSongExists.size() - 1 ? 0 : idx + 1);
			next_dir = directoriesSongExists[next_idx];
		}
		
		if (!next_dir.empty() && (_paths.count(songID) == 0 || _paths[songID] != next_dir))
		{
			_paths[songID] = next_dir;
			label->setString(fmt::format("Folder: {}", next_dir).c_str());
			((CCObject*)this)->setTag(1945);
		}
	}
	
	void onOpenFolder(CCObject* sender)
	{
		ShellExecuteA(NULL, "open", fmt::format("{}", getWritablePath()).c_str(), NULL, NULL, SW_SHOWDEFAULT);
	}
	
	void onCopySongID(CCObject* sender)
	{
		set_clipboard_text(fmt::format("{}", sender->getTag()));
	}
	
	void onDownloadLink(CCObject* sender)
	{
		CCApplication::sharedApplication()->openURL(((SongInfoLayer*)this)->m_downloadLink.c_str());
	}
};

std::vector<std::string_view> CallBacks::directoriesSongExists {};

bool SongInfoLayer_init(SongInfoLayer* self, std::string songName, std::string artistName, std::string downloadLink, std::string artistNG, std::string artistYT, std::string artistFB)
{
	if(!matdash::orig<&SongInfoLayer_init>(self, songName, artistName, downloadLink, artistNG, artistYT, artistFB))
		return false;
	
	auto isCustomSong = [&]() -> bool 
	{
		if(downloadLink.empty()) return false;
		if(downloadLink == "http://www.youtube.com/watch?v=JhKyKEDxo8Q") return false;
		if(downloadLink == "http://www.youtube.com/watch?v=N9vDTYZpqXM") return false;
		if(downloadLink == "http://www.youtube.com/watch?v=4W28wWWxKuQ") return false;
		if(downloadLink == "http://www.youtube.com/watch?v=FnXabH2q2A0") return false;
		if(downloadLink == "http://www.youtube.com/watch?v=TZULkgQPHt0") return false;
		if(downloadLink == "http://www.youtube.com/watch?v=fLnF-QnR1Zw") return false;
		if(downloadLink == "http://www.youtube.com/watch?v=ZXHO4AN_49Q") return false;
		if(downloadLink == "http://www.youtube.com/watch?v=zZ1L9JD6l0g") return false;
		if(downloadLink == "http://www.youtube.com/watch?v=KDdvGZn6Gfs") return false;
		if(downloadLink == "http://www.youtube.com/watch?v=PSvYfVGyQfw") return false;
		if(downloadLink == "http://www.youtube.com/watch?v=D5uJOpItgNg") return false;
		if(songName == "Theory of Everything" && downloadLink == "http://www.newgrounds.com/audio/listen/354826") return false;
		if(downloadLink == "https://www.youtube.com/watch?v=Pb6KyewC_Vg") return false;
		if(songName == "Clubstep" && downloadLink == "http://www.newgrounds.com/audio/listen/396093") return false;
		if(songName == "Electrodynamix" && downloadLink == "http://www.newgrounds.com/audio/listen/368392") return false;
		if(downloadLink == "https://www.youtube.com/watch?v=afwK743PL2Y") return false;
		if(downloadLink == "https://www.youtube.com/watch?v=Z5RufkDHsdM") return false;
		if(downloadLink == "http://www.robtopgames.com/geometricaldominator") return false;
		if(downloadLink == "https://www.youtube.com/watch?v=QRGkFkf2r0U") return false;
		if(downloadLink == "https://www.youtube.com/watch?v=BuPmq7yjDnI") return false;
		if(downloadLink == "http://www.youtube.com/watch?v=5Epc1Beme90") return false;
		
		return true;
	};
	
	if(!isCustomSong()) return true;
	
	
	
	
	auto pos = downloadLink.find_last_of('/');
	if(pos == std::string::npos)
		return true;
	
	std::string idStr = downloadLink.substr(pos + 1);
	fmt::print("idStr: {}\n", idStr);
	int songID = 0;
	auto result = std::from_chars(idStr.data(), idStr.data() + idStr.size(), songID);
	if(result.ec != std::errc())
		return true;
	
	
	//remove the download soundtrack button
	auto menu = MBO(CCMenu*, self, 0x198);
	{
		auto children = menu->getChildren();
		int count = children->count();
		for(int i = 0; i < count; i++)
		{
			if (auto menu_item_node = dynamic_cast<CCMenuItem*>(children->objectAtIndex(i)); menu_item_node) 
			{
				const auto selector = public_cast(menu_item_node, m_pfnSelector);
				const auto addr = (union_cast<void*>(selector));
				if(addr == (void*)0xC01260)
					menu_item_node->setPositionY(100000);
				
				std::cout << addr << '\n';
			}
		}
	}
	auto midpos = CCDirector::sharedDirector()->getWinSize() / 2;
	
	constexpr float offsetX = 125.0f;
	constexpr float offsetY = 15.0f;
	constexpr float scale = 0.46f;
	
	auto spr = ButtonSprite::create("Download", 220, false, "bigFont.fnt", "GJ_button_01.png", 0.0f, scale);
	auto btn = CCMenuItemSpriteExtra::create(spr, self, menu_selector(CallBacks::onDownloadLink));
	btn->setPosition(menu->convertToNodeSpace({midpos.width, midpos.height + offsetY}));
	menu->addChild(btn);
	
	spr = ButtonSprite::create("Copy Song ID", 220, false, "bigFont.fnt", "GJ_button_01.png", 0.0f, scale);
	btn = CCMenuItemSpriteExtra::create(spr, self, menu_selector(CallBacks::onCopySongID));
	btn->setTag(songID);
	btn->setPosition(menu->convertToNodeSpace({midpos.width - offsetX, midpos.height + offsetY}));
	menu->addChild(btn);
	
	spr = ButtonSprite::create("Open Appdata", 220, false, "bigFont.fnt", "GJ_button_01.png", 0.0f, scale);
	btn = CCMenuItemSpriteExtra::create(spr, self, menu_selector(CallBacks::onOpenFolder));
	btn->setPosition(menu->convertToNodeSpace({midpos.width + offsetX, midpos.height + offsetY}));
	menu->addChild(btn);
	
		//update dirs
	_directories = getDirectories(getWritablePath());
	CallBacks::directoriesSongExists.clear();
	for(std::string_view dir : _directories)
	{
		if(std::filesystem::exists(fmt::format("{}{}\\{}.mp3", getWritablePath(), dir, songID)))
		{
			CallBacks::directoriesSongExists.push_back(dir);
		}
	}
	
	if(CallBacks::directoriesSongExists.size() == 0)
		return true;

	CallBacks::directoriesSongExists.push_back("none");

	fmt::print("downloadLink: {} song id init: {}\n", downloadLink, songID);
	
	auto layer = (CCLayer*)self->getChildren()->objectAtIndex(0);
	layer->setTag(songID);
	
	auto it = _paths.find(songID);
	std::string dirName {}; 
	if(it != _paths.end())
	{
		if(std::filesystem::exists(fmt::format("{}{}\\{}.mp3", getWritablePath(), it->second, songID)))
		{
			dirName = it->second;
		}
		else
		{
			_paths.erase(it);
			_paths[songID] = "none";
			dirName = "none";
		}
	}
	else
	{
		_paths[songID] = "none";
		dirName = "none";
	}
	
	
	auto label = CCLabelBMFont::create(fmt::format("Folder: {}", dirName).c_str(), "bigFont.fnt");
	label->setTag(songID);
	label->setScale(0.5f);
	label->setPosition({midpos.width, midpos.height - 25.0f});
	layer->addChild(label);
	
	constexpr float sep = 150.0f;
	
	auto btnLeft = CCMenuItemSpriteExtra::create(CCSprite::createWithSpriteFrameName("edit_leftBtn_001.png"), self, menu_selector(CallBacks::onArrow));
	btnLeft->setTag(-1);
	btnLeft->setPosition(menu->convertToNodeSpace({midpos.width - sep, midpos.height - 25.0f}));
	menu->addChild(btnLeft);
	
	auto btnRight = CCMenuItemSpriteExtra::create(CCSprite::createWithSpriteFrameName("edit_rightBtn_001.png"), self, menu_selector(CallBacks::onArrow));
	btnRight->setTag(1);
	btnRight->setPosition(menu->convertToNodeSpace({midpos.width + sep, midpos.height - 25.0f}));
	menu->addChild(btnRight);
	
	
	fmt::print("{}, {}\n", songName, songID);
	return true;
}

void SongInfoLayer_onClose(CCObject* self, void* sender)
{
	if(self->getTag() == 1945)
	{
		fmt::print("saving to file\n");
		std::ofstream out_file(getSaveFilePath(), std::ofstream::binary);
		hps::to_stream(_paths, out_file);
	}
	matdash::orig<&SongInfoLayer_onClose>(self, sender);
}


void mod_main(HMODULE) {
	
	#ifdef USE_WIN32_CONSOLE
		if(AllocConsole()) {
			freopen("CONOUT$", "wt", stdout);
			freopen("CONIN$", "rt", stdin);
			freopen("CONOUT$", "w", stderr);
			std::ios::sync_with_stdio(1);
		}
	#endif
	//matdashsh::add_hook<&GJDropDownLayer_init>(base + 0x113530);
	//matdash::add_hook<&CustomSongWidget_init>(base + 0x685b0);
	matdash::add_hook<&MusicDownloadManager_pathForSong>(gd::base + 0x1960E0);
	//matdash::add_hook<&MenuLayer_onNewgrounds>(gd::base + 0x191e90);
	matdash::add_hook<&LoadingLayer_getLoadingString>(gd::base + 0x18cf40);
	matdash::add_hook<&SongInfoLayer_init>(gd::base + 0x250A80);
	matdash::add_hook<&SongInfoLayer_onClose>(gd::base + 0x49C60);
}