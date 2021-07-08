/**
 * @file llfloater360capture.cpp
 * @author Callum Prentice
 * @brief Floater for the 360 capture feature
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

/**
 * Floater that appears when buying an object, giving a preview
 * of its contents and their permissions.
 */

#include "llviewerprecompiledheaders.h"

#include "llfloater360capture.h"

#include "llagent.h"
#include "llagentcamera.h"
#include "llagentui.h"
#include "lleventcoro.h"
#include "llimagejpeg.h"
#include "llmediactrl.h"
#include "lltrans.h"
#include "llslurl.h"
#include "lltextbox.h"
#include "lluictrlfactory.h"
#include "llversioninfo.h"
#include "llviewercamera.h"
#include "llviewercontrol.h"
#include "llviewerpartsim.h"
#include "llviewerregion.h"
#include "llviewerwindow.h"
#include "pipeline.h"

LLFloater360Capture::LLFloater360Capture(const LLSD& key)
	:	LLFloater(key)
{
	mWebBrowser = 0;
	mFloaterWidth = 0;
	mFloaterHeight = 0;
}

LLFloater360Capture::~LLFloater360Capture()
{
	if (mWebBrowser)
	{
		mWebBrowser->navigateStop();
		mWebBrowser->clearCache();
		mWebBrowser->unloadMediaSource();
	}
}

BOOL LLFloater360Capture::postBuild()
{
	mCaptureBtn = getChild<LLUICtrl>("capture_button");
	mCaptureBtn->setCommitCallback(boost::bind(&LLFloater360Capture::onCapture360ImagesBtn, this));

	mSaveLocalBtn = getChild<LLUICtrl>("save_local_button");
	mSaveLocalBtn->setCommitCallback(boost::bind(&LLFloater360Capture::onSaveLocalBtn, this));
	mSaveLocalBtn->setEnabled(false);

	mCloseBtn = getChild<LLUICtrl>("close_button");
	mCloseBtn->setCommitCallback(boost::bind(&LLFloater360Capture::onCloseBtn, this));

	mWebBrowser = getChild<LLMediaCtrl>("360capture_contents");
	mWebBrowser->addObserver(this);

	mStatusBarText = getChild<LLTextBox>("status_bar_text");

	// store the size (width and height) of the 6 source images used to generate
	// the equirectangular image in settings for now.  We might expose via
	// the UI eventually
	mSourceImageSize = gSavedSettings.getU32("360SnapShotSourceImageSize");

	LLRect window_rect = gViewerWindow->getWindowRectRaw(); 

	S32 window_width  = window_rect.getWidth();
	S32 window_height = window_rect.getHeight();

	// Need to limit source image size to smallest dim of window due to non-ALM rendering
	// using the default frame buffer to avoid fitting problems
	if (!LLPipeline::sRenderDeferred)
	{
		while (mSourceImageSize > window_width || mSourceImageSize > window_height)
		{
			mSourceImageSize /= 2;
		}
		llassert(mSourceImageSize > 0);
	}

	// the size of the output equirectangular image.  The width is always 2x
	// the height.  TODO: Need to propagate this through to JavaScript EQR code
	mOutputImageWidth = gSavedSettings.getU32("360SnapShotOutputImageWidth");
	mOutputImageHeight = gSavedSettings.getU32("360SnapShotOutputImageHeight");

	// determine whether or not to include avatar in the scene as we capture
	// the 360 snapshot. We might expose this via UI eventually
	mHideAvatar = gSavedSettings.getBOOL("360SnapShotHideAvatar");

	// enable resizing and enable for width and for height
	enableResizeCtrls(true, true, true);

	// initial heading that consumers of the equirectangular image
	// (such as Facebook or Flickr) use to position initial view - 
	// we set during capture
	mInitialHeadingDeg = 0.0;

	// initial contents of status bar
	setStatusText("360CaptureReadyToCapture");

	// save directory in which to store the images (must obliviously be writable by the viewer)
	// Also create it for users who haven't used the 360 feature before.
	mImageSaveDir = gDirUtilp->getLindenUserDir() + gDirUtilp->getDirDelimiter() + "eqrimg";
	LLFile::mkdir(mImageSaveDir);

	// initial size of floater
	mFloaterWidth = getRect().getWidth();
	mFloaterHeight = getRect().getHeight();

	// start with web contents hidden because there is nothing to show
	showWebPanel(false);

    for (int i = 0; i < 6; i++)
    {
        raw_images[i] = new LLImageRaw(mSourceImageSize, mSourceImageSize, 3);
    }

	return TRUE;
}

const std::string LLFloater360Capture::getHTMLBaseFolder()
{
	std::string folder_name = gDirUtilp->getSkinDir();
	folder_name += gDirUtilp->getDirDelimiter();
	folder_name += "html";
	folder_name += gDirUtilp->getDirDelimiter();
	folder_name += "common";
	folder_name += gDirUtilp->getDirDelimiter();
	folder_name += "equirectangular";
	folder_name += gDirUtilp->getDirDelimiter();

	return folder_name;
}

void LLFloater360Capture::onCapture360ImagesBtn()
{
	capture360Images();
}

void LLFloater360Capture::encodeCap(LLPointer<LLImageRaw> raw_image, const char* filename)
{
    LLPointer<LLImageJPEG> jpeg_image = new LLImageJPEG();
	if (jpeg_image->encode(raw_image, 0))
	{
		std::string image_filename = mImageSaveDir;
		image_filename += gDirUtilp->getDirDelimiter();
		image_filename += filename;
		image_filename += ".jpg";

		LL_INFOS() << "360 capture - save source image " << image_filename << LL_ENDL;
		jpeg_image->save(image_filename);        
	}
}

static const std::string filenames[6] =
{
    "posx", "posz", "posy",
    "negx", "negz", "negy",
};

void LLFloater360Capture::capture360Images()
{
	// disable buttons while we are capturing
	mCaptureBtn->setEnabled(false);
	mSaveLocalBtn->setEnabled(false);

	// hide web content while we capture - more to not have things
	// feel weird the first time users use this and there is nothing
	// in the web content
	showWebPanel(false);

	// display helpful, reassuring yet unobtrusive status message
	// Note: Sadly, this text is never displayed because we don't get another
	// call to draw() between now and when the capture completes and new text is set
	// Trying to obviate this by calling draw() results in an llerrs() in the shader code :(
	setStatusText("360CaptureCapturing360Images");

	// optionally remove avatar from the scene (save current state first so we can restore it later)
	BOOL cur_show_avatar = gAgent.getShowAvatar();
	if (mHideAvatar)
	{
		gAgent.setShowAvatar(FALSE);
	}

	// these are the 6 directions we will point the camera
	LLVector3 look_dirs[6] = { LLVector3(1, 0, 0), LLVector3(0, 1, 0), LLVector3(0, 0, 1), LLVector3(-1, 0, 0), LLVector3(0, -1, 0), LLVector3(0, 0, -1) };
	LLVector3 look_upvecs[6] = { LLVector3(0, 0, 1), LLVector3(0, 0, 1), LLVector3(0, -1, 0), LLVector3(0, 0, 1), LLVector3(0, 0, 1), LLVector3(0, 1, 0) };

	// save current view/camera settings
	S32 old_occlusion = LLPipeline::sUseOcclusion;
	LLPipeline::sUseOcclusion = 0;
	LLViewerCamera* camera = LLViewerCamera::getInstance();
	F32 old_fov = camera->getView();
	F32 old_aspect = camera->getAspect();

	// stop the motion of as much of the world moving as we can
	freezeWorld(true);

	// save the direction (in degrees) the camera is looking when we take the shot since that is what we write to image metadata
	mInitialHeadingDeg = camera->getYaw() * RAD_TO_DEG;

	// camera constants
	camera->setAspect(1.0); // must set aspect ratio first to avoid undesirable clamping of vertical FoV
	camera->setView(F_PI_BY_TWO);
	camera->yaw(0.0);
	// TODOCP   reset these when floater canceled or closed  (save original values before we start capture)

	// record how many times we changed camera to try to understand the "all shots are the same issue"
	unsigned int camera_changed_times = 0;

	// for each of the 6 directions we shoot...
	for (int i = 0; i < 6; i++)
	{
		// set up camera to look in each direction
		camera->lookDir(look_dirs[i], look_upvecs[i]);

		// record if camera changed to try to understand the "all shots are the same issue"
		if (camera->isChanged())
		{
			++camera_changed_times;
		}

		// take a snapshot of color
		bool keep_window_aspect = FALSE;
		bool is_texture = TRUE;
		bool show_ui = FALSE;
		bool do_rebuild = TRUE;
		gViewerWindow->rawSnapshot(
                            raw_images[i],
			                mSourceImageSize,
			                mSourceImageSize,
			                keep_window_aspect,
			                is_texture,
			                show_ui,
			                do_rebuild);

		// save the RAW as a JPEG image with best possible quality		
		LLCoros::instance().launch("encode360cap", boost::bind(&LLFloater360Capture::encodeCap, this, raw_images[i], filenames[i].c_str()));
	}

	// unfreeze the world now we have our shots
	freezeWorld(false);

	// restore original view/camera/avatar settings settings
	camera->setAspect(old_aspect);
	camera->setView(old_fov);
	
	LLPipeline::sUseOcclusion = old_occlusion;
	gAgent.setShowAvatar(cur_show_avatar);

	// record that we missed some shots in the log for later debugging
	// note: we use 5 and not 6 because the first shot isn't regarded as a change - only the subsequent 5 are
	if (camera_changed_times < 5)
	{
		LL_INFOS() << "Warning: we only captured " << camera_changed_times << " images." << LL_ENDL;
	}

	// now we have the 6 shots saved in a well specified location,
	// we can load the web content that uses them
	std::string url = "file:///" + getHTMLBaseFolder() + mDisplayEqrImageHTML;
	mWebBrowser->navigateTo(url);

	// display helpful, reassuring yet unobtrusive status message to indicate images are captured
	setStatusText("360CaptureCaptured360Images");
}

void LLFloater360Capture::handleMediaEvent(LLPluginClassMedia* self, EMediaEvent event)
{
	switch (event)
	{
	case MEDIA_EVENT_LOCATION_CHANGED:
		break;

	case MEDIA_EVENT_NAVIGATE_BEGIN:
		break;

	case MEDIA_EVENT_NAVIGATE_COMPLETE:
	{
		std::string navigate_url = self->getNavigateURI();
		// even though this is the only page we visit in this floater, play it safe
		// and compare the URL we navigated to with the page we care about
		if (navigate_url.find(mDisplayEqrImageHTML) != std::string::npos)
		{
			// Debugging - open JS console.
			//mWebBrowser->getMediaPlugin()->showWebInspector(true);

			// this string is being passed across to the web so replace all the windows backslash
			// characters with forward slashes or (I think) the backslashes are treated as escapes
			std::replace(mImageSaveDir.begin(), mImageSaveDir.end(), '\\', '/');

			// so now our page is loaded and images are in place - call the JS init script
			// with some params to render the cube map comprising of the images
			std::ostringstream cmd;
			cmd << "init(";
			cmd << mOutputImageWidth;
			cmd << ", ";
			cmd << mOutputImageHeight;
			cmd << ", ";
			cmd << "'";
			cmd << mImageSaveDir;
			cmd << "'";
			cmd << ")";

			// execute the command on the page
			mWebBrowser->getMediaPlugin()->executeJavaScript(cmd.str());

			// tell user we have finished capture
			setStatusText("360CaptureComplete");

			// page is loaded and ready so we can turn on the buttons again
			mCaptureBtn->setEnabled(true);
			mSaveLocalBtn->setEnabled(true);

			// now we have some web content, we should show it
			showWebPanel(true);
		}
	}
	break;

	default:
		break;
	}
}

void LLFloater360Capture::onSaveLocalBtn()
{
	// tell the user what we are doing
	setStatusText("360CaptureSaving360Output");

	// region name and URL
	std::string region_name(""); // no sensible default
	std::string region_url("http://secondlife.com");
	LLViewerRegion* region = gAgent.getRegion();
	if (region)
	{
		region_name = region->getName();
		LLSLURL slurl;
		LLAgentUI::buildSLURL(slurl);
		region_url = slurl.getSLURLString();
	};

	// build and add the Extensible Metadata Protocol block that describes the contents of the image
	const std::string xmp = build_xmp_block(region_name, region_url, mOutputImageWidth, mOutputImageHeight, mInitialHeadingDeg);

	// build suggested filename
	const std::string suggested_filename = generate_proposed_filename();

	// build the JavaScript command to send to the web browser
	const std::string cmd = "saveAsEqrImage(\"" + suggested_filename + "\", \"" + xmp + "\")";

	// send it to the browser instance, triggering the equirectangular capture
	mWebBrowser->getMediaPlugin()->executeJavaScript(cmd);

	// clear stats text
	setStatusText("");
}

void LLFloater360Capture::onCloseBtn()
{
	closeFloater();
}

void LLFloater360Capture::showWebPanel(bool show)
{
	if (show)
	{
		mWebBrowser->setVisible(true);

		reshape(mFloaterWidth, mFloaterHeight);

		setCanResize(true);
	}
	else
	{
		mWebBrowser->setVisible(false);

		const int closed_floater_height = 75;
		reshape(mFloaterWidth, closed_floater_height);

		setCanResize(false);
	}
}

void LLFloater360Capture::handleReshape(const LLRect& new_rect, bool by_user)
{
	// only save the size when the control is visible - this function is called:
	// when the floater is moved too and we don't want to save the closed size
	if (mWebBrowser && mWebBrowser->getVisible())
	{
		mFloaterWidth = new_rect.getWidth();
		mFloaterHeight = new_rect.getHeight();
	}

	LLFloater::handleReshape(new_rect, by_user);
}

void LLFloater360Capture::freezeWorld(bool enable)
{
	if (enable)
	{
		// freeze all avatars
		LLCharacter* avatarp;
		for (std::vector<LLCharacter*>::iterator iter = LLCharacter::sInstances.begin();
			iter != LLCharacter::sInstances.end(); ++iter)
		{
			avatarp = *iter;
			mAvatarPauseHandles.push_back(avatarp->requestPause());
		}

		// freeze everything else
		gSavedSettings.setBOOL("FreezeTime", TRUE);

		//disable particle system
		LLViewerPartSim::getInstance()->enable(false);
	}
	else // turning off freeze world mode, either temporarily or not.
	{
		// thaw all avatars
		mAvatarPauseHandles.clear();

		// thaw everything else
		gSavedSettings.setBOOL("FreezeTime", FALSE);

		//enable particle system
		LLViewerPartSim::getInstance()->enable(true);
	}
}

void LLFloater360Capture::setStatusText(const std::string text_id)
{
	// removing status text display until we figure out a way
	// to update it during capture process. (keeping it in the compile
	// so we don't introduce build errors
	const bool status_text_enabled = true;
	if (status_text_enabled)
	{
		// if strings.xml label is empty string, just clear text
		if (text_id.length() == 0)
		{
			mStatusBarText->setValue("");
		}
		else
		// look up value in strings.xml and display
		{
			const std::string status_text = LLTrans::getString(text_id);
			mStatusBarText->setValue(status_text);
		}
	}
}

const std::string LLFloater360Capture::generate_proposed_filename()
{
	std::ostringstream filename("");

	// base name
	filename << "sl360_";

	LLViewerRegion* region = gAgent.getRegion();
	if (region)
	{
		// this looks complex but it's straightforward - removes all non-alpha chars from a string
		// which in this case is the SL region name - we use it as a proposed filename but the user is free to change
		std::string region_name = region->getName();
		std::replace_if(region_name.begin(), region_name.end(), std::not1(std::ptr_fun(isalnum)), '_');
		if (region_name.length() > 0)
		{
			filename << region_name;
			filename << "_";
		}
	}

	// add in resolutiion to make it easier to tell what you captured later
	filename << mOutputImageWidth;
	filename << "x";
	filename << mOutputImageHeight;
	filename << "_";

	// add in the current HH-MM-SS (with leading 0's) so users can easily save many shots in same folder
	std::time_t cur_epoch = std::time(nullptr);
	std::tm* tm_time = std::localtime(&cur_epoch);
	filename << std::setfill('0') << std::setw(2) << tm_time->tm_mday;
	filename << std::setfill('0') << std::setw(2) << tm_time->tm_hour;
	filename << std::setfill('0') << std::setw(2) << tm_time->tm_min;
	filename << std::setfill('0') << std::setw(2) << tm_time->tm_sec;

	// it's a jpeg file
	// see SL-753 to learn why we use "jpg" over "jpeg"
	filename << ".jpg";

	return filename.str();
}

const std::string LLFloater360Capture::build_xmp_block(std::string region_name,
	std::string region_url,
	int img_width,
	int img_height,
	double initial_heading)
{
	// required data block
	const std::string xml_header1("http://ns.adobe.com/xap/1.0/.<?xpacket begin='' id='W5M0MpCehiHzreSzNTczkc9d'?>");
	const std::string xml_header2a("<x:xmpmeta xmlns:x='adobe:ns:meta/' x:xmptk='");
	const std::string xml_header2b("'>");
	const std::string xml_header3("<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>");
	const std::string xml_header4("<rdf:Description rdf:about='' xmlns:GPano='http://ns.google.com/photos/1.0/panorama/'>");
	const std::string xml_footer1("</rdf:Description>");
	const std::string xml_footer2("</rdf:RDF>");
	const std::string xml_footer3("</x:xmpmeta>");

	// define creator as *this* version of the viewer
	std::ostringstream xmp_toolkit("");
	xmp_toolkit << "Second Life Viewer";
	xmp_toolkit << " ";
	xmp_toolkit << LLVersionInfo::instance().getShortVersion() << "." << LLVersionInfo::instance().getBuild();

	std::ostringstream xml_block("");

	xml_block << xml_header1;
	xml_block << xml_header2a;
	xml_block << xmp_toolkit.str();
	xml_block << xml_header2b;
	xml_block << xml_header3;
	xml_block << xml_header4;

	// this is what tells third party consumers it's a 360
	xml_block << "<GPano:ProjectionType>equirectangular</GPano:ProjectionType>";
	xml_block << "<GPano:UsePanoramaViewer>True</GPano:UsePanoramaViewer>";

	// capture software is the viewer (with version)
	xml_block << "<GPano:CaptureSoftware>" << xmp_toolkit.str() << "</GPano:CaptureSoftware>";

	// stitching software is the viewer
	xml_block << "<GPano:StitchingSoftware>" << xmp_toolkit.str() << "</GPano:StitchingSoftware>";

	// record the size of the original cube map image (not really required but useful for debugging quality)
	xml_block << "<GPano:SourceCubeMapSizePixels>" << mSourceImageSize << "</GPano:SourceCubeMapSizePixels>";

	// how big the image is and which direction to start looking
	xml_block << "<GPano:InitialViewHeadingDegrees>" << initial_heading << "</GPano:InitialViewHeadingDegrees>";
	xml_block << "<GPano:FullPanoWidthPixels>" << img_width << "</GPano:FullPanoWidthPixels>";
	xml_block << "<GPano:FullPanoHeightPixels>" << img_height << "</GPano:FullPanoHeightPixels>";

	// when it was taken (inside the viewer)
	std::time_t result = std::time(nullptr);
	std::string ctime_str = std::ctime(&result);
	std::string time_str = ctime_str.substr(0, ctime_str.length() - 1);
	xml_block << "<GPano:FirstPhotoDate>" << time_str << "</GPano:FirstPhotoDate>";
	xml_block << "<GPano:LastPhotoDate>" << time_str << "</GPano:LastPhotoDate>";

	// we store this here too so the (internal, temp) web viewer can access them.
	// we'll commit to leaving them here so other applications can also use them
	xml_block << "<SLRegionName>" << region_name << "</SLRegionName>";
	xml_block << "<SLRegionURL>" << region_url << "</SLRegionURL>";

	// we already changed the format from zip to jpeg so it seems useful to add a version
	// number in case we have to branch based on granular features
	const std::string pano_version("2.1.0");
	xml_block << "<SLPanoVersion>" << pano_version << "</SLPanoVersion>";

	xml_block << xml_footer1;
	xml_block << xml_footer2;
	xml_block << xml_footer3;

	return xml_block.str();
}
