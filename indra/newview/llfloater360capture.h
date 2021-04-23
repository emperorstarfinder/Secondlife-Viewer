/** 
 * @file llfloateravatar.h
 * @author Leyla Farazha
 * @brief floater for the avatar changer
 *
 * $LicenseInfo:firstyear=2011&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2011, Linden Research, Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * 
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#ifndef LL_FLOATER_360CAPTURE_H
#define LL_FLOATER_360CAPTURE_H

#include "llfloater.h"
#include "llhandle.h"
#include "llmediactrl.h"
#include "llcharacter.h"

class LLImageRaw;
class LLImageJPEG;
class LLTextBox;

class LLFloater360Capture:
	public LLFloater,
	public LLViewerMediaObserver
{
	friend class LLFloaterReg;

	private:
		LLFloater360Capture(const LLSD& key);
		
		~LLFloater360Capture();
		BOOL postBuild() override;
		void handleReshape(const LLRect& new_rect, bool by_user = false) override;
		void handleMediaEvent(LLPluginClassMedia* self, EMediaEvent event) override;

		const std::string getHTMLBaseFolder();
		void capture360Images();
		
		void capture360ImagesDraw();

		void encodeCap(LLPointer<LLImageRaw> raw_image, const char* filename);

		std::vector<LLAnimPauseRequest> mAvatarPauseHandles;
		void freezeWorld(bool enable);
		void setStatusText(const std::string text);

		const std::string build_xmp_block(std::string region_name, std::string region_url, int img_width, int img_height, double initial_heading);
		const std::string generate_proposed_filename();

		void showWebPanel(bool show);

		LLMediaCtrl* mWebBrowser;
		const std::string mDisplayEqrImageHTML = "display_eqr.html";

		LLTextBox* mStatusBarText;

		LLUICtrl* mCaptureBtn;
		void onCapture360ImagesBtn();

		void onSaveLocalBtn();
		LLUICtrl* mSaveLocalBtn;

		void onCloseBtn();

		LLUICtrl* mCloseBtn;
		int mFloaterWidth;
		int mFloaterHeight;
		int mSourceImageSize;
		bool mHideAvatar;
		float mInitialHeadingDeg;
		int mOutputImageWidth;
		int mOutputImageHeight;
		std::string mImageSaveDir;

		LLPointer<LLImageRaw> raw_images[6];
};

#endif  // LL_FLOATER_360CAPTURE_H
