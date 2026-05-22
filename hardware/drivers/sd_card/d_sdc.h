



#pragma once

#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/spi_master.h"

#ifndef D_SDC_H
#define D_SDC_H

//content

 typedef enum{
        OFV_TXT, //the most normal thing here bro
        OFV_WEBPAGE, //read the html of web servers this thing can pull up or view it
        OFV_IMG, //entrypoint
        OFV_AUDIO, //listen to mp3 or wav or whatnot
        OFV_VIDEO, //see a video file (no mp4 because decode takes forever)
        OFV_STREAMED_AUDIO, //call people
        OFV_STREAMED_VIDEO, //remote cam
        OFV_3DOBJ, //view an .obj file or shitty scan
        OFV_XR, //future xr overlay for alt modes or headset/target delegate?
        OFV_CANVAS_ANIM_REPLAY, //REPLAY an animation made on device by shapes in the canvas object. future use
        OFV_RAW, //raw file types  that are application specific
         OFV_COUNT
        }OFV_Mode; 
        
        
        typedef struct{
    const char* ext;
    OFV_Mode mode;
} OFV_ExtMap;

static const OFV_ExtMap g_ofv_ext_map[] = {

	//specials for apps
	{"ptbl" ,OFV_RAW},//table of pointers
	{"nfcd" ,OFV_RAW}, //nfc card data
	{"duck",OFV_RAW}, //my own stupid rubberducky script externsion
	{"ramst",OFV_RAW}, //ram state saved ?
	{"arr",OFV_RAW}, //raw array


    // =========================
    // TEXT / DOCUMENTS
    // =========================
    {".txt",   OFV_TXT},
    {".md",    OFV_TXT},
    {".json",  OFV_TXT},
    {".xml",   OFV_TXT},
    {".yaml",  OFV_TXT},
    {".yml",   OFV_TXT},
    {".ini",   OFV_TXT},
    {".cfg",   OFV_TXT},
    {".conf",  OFV_TXT},
    {".log",   OFV_TXT},
    {".csv",   OFV_TXT},
    {".c",     OFV_TXT}, //can't really compile c onthe device but we can maybe emulate it
    {".h",     OFV_TXT},
    {".cpp",   OFV_TXT},
    {".hpp",   OFV_TXT},
    {".py",    OFV_TXT},
    {".js",    OFV_TXT},
    {".ts",    OFV_TXT},
    {".java",  OFV_TXT},
    {".cs",    OFV_TXT},
    {".sh",    OFV_TXT},
    {".bat",   OFV_TXT},
    {".lua",   OFV_TXT},
    {".rs",    OFV_TXT},
    

    // =========================
    // WEB
    // =========================
    {".html",  OFV_WEBPAGE},
    {".htm",   OFV_WEBPAGE},
    {".xhtml", OFV_WEBPAGE},

    // =========================
    // IMAGES
    // =========================
    {".png",   OFV_IMG},
    {".jpg",   OFV_IMG},
    {".jpeg",  OFV_IMG},
    {".bmp",   OFV_IMG},
    {".gif",   OFV_IMG},
    {".webp",  OFV_IMG},
    {".tga",   OFV_IMG},
    {".psd",   OFV_IMG},
    {".hdr",   OFV_IMG},
    {".ico",   OFV_IMG},
    {".svg",   OFV_IMG},

    // =========================
    // AUDIO
    // =========================
    {".wav",   OFV_AUDIO},
    {".ogg",   OFV_AUDIO},
    {".mp3",   OFV_AUDIO},
    {".flac",  OFV_AUDIO},
    {".aac",   OFV_AUDIO},
    {".m4a",   OFV_AUDIO},
    {".opus",  OFV_AUDIO},
    {".mid",   OFV_AUDIO},
    {".midi",  OFV_AUDIO},

    // =========================
    // VIDEO
    // =========================
    {".avi",   OFV_VIDEO},
    {".mkv",   OFV_VIDEO},
    {".webm",  OFV_VIDEO},
    {".mov",   OFV_VIDEO},
    {".ogv",   OFV_VIDEO},
	{".mp4",OFV_VIDEO},
    // .mp4 -> decode takes forever DONT use it  lol

    // =========================
    // STREAMING / NETWORK MEDIA
    // =========================
    {".sdp",   OFV_STREAMED_AUDIO},
    {".webrtc",OFV_STREAMED_VIDEO},
    {".rtsp",  OFV_STREAMED_VIDEO},
    {".m3u8",  OFV_STREAMED_VIDEO},

    // =========================
    // 3D OBJECTS
    // =========================
    {".obj",   OFV_3DOBJ},
    {".fbx",   OFV_3DOBJ},
    {".gltf",  OFV_3DOBJ},
    {".glb",   OFV_3DOBJ},
    {".ply",   OFV_3DOBJ},
    {".stl",   OFV_3DOBJ},
    {".dae",   OFV_3DOBJ},
    {".3ds",   OFV_3DOBJ},

    // =========================
    // XR / SCENE
    // =========================
    {".vrm",   OFV_XR},
    {".usdz",  OFV_XR},
    {".xrsc", OFV_XR},

    // =========================
    // CANVAS ANIMATION REPLAY
    // =========================
    {".ofca",  OFV_CANVAS_ANIM_REPLAY},
    {".canim", OFV_CANVAS_ANIM_REPLAY},

};



#endif