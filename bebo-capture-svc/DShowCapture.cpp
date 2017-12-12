#include "DShowCapture.h"

#include <initguid.h>
#include <streams.h>	// for DirectShow headers
#include <wmcodecdsp.h>	// for MEDIASUBTYPE_MPEG_ADTS_AAC
#include <mmreg.h>		// for WAVE_FORMAT_MPEG_ADTS_AAC
#include <dvdmedia.h>	// for VIDEOINFOHEADER2
#include <bdaiface.h>	// for IMPEG2PIDMap
#include "logging.h"
#include <libyuv/convert.h>

#include "IVideoCaptureFilterTypes.h"
#include "IVideoCaptureFilter.h"

DShowCapture::DShowCapture():
	sinkFilter(new SinkFilter(this)),
	initialized(false)
{
}

DShowCapture::~DShowCapture() {

}

#if 1

namespace pmt_log {

	int DisplayRECT(wchar_t *buffer, size_t count, const RECT& rc)
	{
		return _snwprintf(buffer, count, L"%dx%d[%d:%d]",
			rc.right - rc.left,
			rc.top - rc.bottom,
			rc.right,
			rc.bottom);
	}

	int DisplayBITMAPINFO(wchar_t *buffer, size_t count, const BITMAPINFOHEADER* pbmi)
	{
		if (pbmi->biCompression < 256) {
			return _snwprintf(buffer, count, L"[bitmap: %dx%dx%d bit  (%d)] size:%d (%d/%d)",
				pbmi->biWidth,
				pbmi->biHeight,
				pbmi->biBitCount,
				pbmi->biCompression,
				pbmi->biSizeImage,
				pbmi->biPlanes,
				pbmi->biClrUsed);
		}
		else {
			// TOOD cant test the biCompression oddity and compiler complains
			//return snprintf(buffer, count, "[bitmap: %dx%dx%d bit '%4.4hx' size:%d (%d/%d)",
			return _snwprintf(buffer, count, L"[bitmap: %dx%dx%d bit '?' size:%d (%d/%d)",
				pbmi->biWidth,
				pbmi->biHeight,
				pbmi->biBitCount,
				//&pbmi->biCompression,
				pbmi->biSizeImage,
				pbmi->biPlanes,
				pbmi->biClrUsed);
		}
	}


	int sprintf_pmt(wchar_t *buffer, size_t count, char *label, const AM_MEDIA_TYPE *pmtIn)
	{
		int cnt = 0;

		cnt += _snwprintf(&buffer[cnt], count - cnt, L"%S -", label);

		wchar_t * temporalCompression = (pmtIn->bTemporalCompression) ? L"Temporally compressed" : L"Not temporally compressed";
		cnt += _snwprintf(&buffer[cnt], count - cnt, L" [%s]", temporalCompression);

		if (pmtIn->bFixedSizeSamples) {
			cnt += _snwprintf(&buffer[cnt], count - cnt, L" [Sample Size %d]", pmtIn->lSampleSize);
		}
		else {
			cnt += _snwprintf(&buffer[cnt], count - cnt, L" [Variable size samples]");
		}

		WCHAR major_uuid[64];
		WCHAR sub_uuid[64];
		StringFromGUID2(pmtIn->majortype, major_uuid, 64);
		StringFromGUID2(pmtIn->subtype, sub_uuid, 64);

		cnt += _snwprintf(&buffer[cnt], count - cnt, L" [%s/%s]",
			major_uuid, sub_uuid);

		if (pmtIn->formattype == FORMAT_VideoInfo) {

			VIDEOINFOHEADER *pVideoInfo = (VIDEOINFOHEADER *)pmtIn->pbFormat;

			cnt += _snwprintf(&buffer[cnt], count - cnt, L" srcRect:");
			cnt += DisplayRECT(&buffer[cnt], count - cnt, pVideoInfo->rcSource);
			cnt += _snwprintf(&buffer[cnt], count - cnt, L" dstRect:");
			cnt += DisplayRECT(&buffer[cnt], count - cnt, pVideoInfo->rcTarget);
			cnt += DisplayBITMAPINFO(&buffer[cnt], count - cnt, HEADER(pmtIn->pbFormat));

		}
		else if (pmtIn->formattype == FORMAT_VideoInfo2) {

			VIDEOINFOHEADER2 *pVideoInfo2 = (VIDEOINFOHEADER2 *)pmtIn->pbFormat;

			cnt += _snwprintf(&buffer[cnt], count - cnt, L" srcRect:");
			cnt += DisplayRECT(&buffer[cnt], count - cnt, pVideoInfo2->rcSource);
			cnt += _snwprintf(&buffer[cnt], count - cnt, L" dstRect:");
			cnt += DisplayRECT(&buffer[cnt], count - cnt, pVideoInfo2->rcTarget);
			cnt += DisplayBITMAPINFO(&buffer[cnt], count - cnt, &pVideoInfo2->bmiHeader);
		}
		else {
			WCHAR format_uuid[64];
			StringFromGUID2(pmtIn->formattype, format_uuid, 64);
			cnt += _snwprintf(&buffer[cnt], count - cnt, L" Format type %ls", format_uuid);
		}
		return cnt;
	}

	void debug_pmt(char* label, const AM_MEDIA_TYPE *pmtIn)
	{
		const int SIZE = 10 * 4096;
		wchar_t buffer[SIZE];
		sprintf_pmt(buffer, SIZE, label, pmtIn);
		debug("%ls", buffer);
	}
}

#endif

void DShowCapture::QueryCapabilities() {}
void DShowCapture::EnumFilters() {}

void DShowCapture::CreateFilterGraph() {
	HRESULT hr;

	hr = CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER,
		IID_IFilterGraph, (void**)&graph);

	hr = CoCreateInstance(CLSID_CaptureGraphBuilder2, NULL,
		CLSCTX_INPROC_SERVER,
		IID_ICaptureGraphBuilder2, (void**)&builder);

	hr = builder->SetFiltergraph(graph);

	hr = graph->QueryInterface(IID_IMediaControl, (void**)&mediaControl);
}

void DShowCapture::AddDeviceFilter() {
	HRESULT hr = CoCreateInstance(CLSID_ElgatoVideoCaptureFilter, NULL, CLSCTX_INPROC, IID_IBaseFilter, (void **)&deviceFilter);
	hr = graph->AddFilter(deviceFilter, L"Elgato Game Capture HD");

	hr = builder->GetFiltergraph(&graph);

	IPin *outputPin = NULL;
	hr = builder->FindPin(deviceFilter, PINDIR_OUTPUT, &PIN_CATEGORY_CAPTURE, &MEDIATYPE_Video, TRUE, 0, &outputPin);
	hr = graph->AddFilter(sinkFilter, NULL);

	ComPtr<IAMStreamConfig> stream_config;
	hr = outputPin->QueryInterface(stream_config.GetAddressOf());
	
	if (FAILED(hr)) {
		error("Failed to query stream config");
		return; 
	}

	int count = 0, size = 0;
	hr = stream_config->GetNumberOfCapabilities(&count, &size);
	if (FAILED(hr)) {
		error("Failed to get number of capability");
		return; 
	}

	std::unique_ptr<BYTE[]> caps(new BYTE[size]);

	for (int i = 0; i < count; i++) {
		AM_MEDIA_TYPE *media_type;
		HRESULT hr = stream_config->GetStreamCaps(i, &media_type, caps.get());
		if (hr != S_OK) {
			error("HR:%d, GetStreamCaps != S_OK", hr);
			continue;
		}

		pmt_log::debug_pmt("GetStreamCaps", media_type);
	}

	int index = 1;
	AM_MEDIA_TYPE *media_type_i420;
	hr = stream_config->GetStreamCaps(index, &media_type_i420, caps.get());
	if (FAILED(hr)) {
		error("Failed to get i420 stream caps, index: %d", index);
		return;
	}
	hr = stream_config->SetFormat(media_type_i420);
	if (FAILED(hr)) {
		error("Failed to set format");
		return; 
	}

	sinkInputPin = sinkFilter->GetPin(0);
	hr = graph->ConnectDirect(outputPin, sinkInputPin, NULL);

	info("Add Device Filter HR: %d", hr);
}

void DShowCapture::AddSinkFilter() {

}


bool DShowCapture::Initialize() {
	if (initialized) return true;

	initialized = true;
	CreateFilterGraph();
	AddDeviceFilter();
	AddSinkFilter();
	Run();
	return true;
}

void DShowCapture::Run() {
	HRESULT hr = mediaControl->Run();
	info("DShowCapture::Run() hr: %d", hr);
}

// FillBuffer - Blocking
bool DShowCapture::GetFrame(IMediaSample * sample)
{
	debug("GetFrame - start");

	IMediaSample *queued_sample;
	while (!media_sample_queue_.wait_for_and_pop(queued_sample, 100)) {
		return false;
	}

	debug("GetFrame - got frame from queue");

	uint8_t* out_buffer;
	sample->GetPointer(&out_buffer);

	uint8_t* queued_sample_buffer;
	queued_sample->GetPointer(&queued_sample_buffer);

	long queued_sample_size = queued_sample->GetActualDataLength();
	long out_sample_size = sample->GetSize();

	// debug("ActualDataLength: %ld, Size: %ld", queued_sample->GetActualDataLength(), queued_sample->GetSize());

	int negotiated_width = 1280;
	int negotiated_height = 720;

	int stride_uyvy = 1280 * 2;
	uint8* y = out_buffer;
	int stride_y = negotiated_width;
	uint8* u = out_buffer + (negotiated_width * negotiated_height);
	int stride_u = (negotiated_width + 1) / 2;
	uint8* v = u + ((negotiated_width * negotiated_height) >> 2);
	int stride_v = stride_u;

	libyuv::UYVYToI420(queued_sample_buffer,
		stride_uyvy,
		y,
		stride_y,
		u,
		stride_u,
		v,
		stride_v,
		negotiated_width,
		negotiated_height);
	
	/*
	if (out_sample_size < queued_sample_size) {
		error("out_sample_size < queued_sample_size. %ld < %ld", out_sample_size, queued_sample_size);
		return false;
	}		
	memcpy(out_buffer, queued_sample_buffer, out_sample_size);
	*/


	static REFERENCE_TIME last_end_time = 0;
	REFERENCE_TIME start_frame;
	REFERENCE_TIME end_frame;
	queued_sample->GetTime(&start_frame, &end_frame);

#if FIX_TIMESTAMP
	debug("start: %lld, stop: %lld, delta: %lld", start_frame, end_frame, end_frame - start_frame);
	if (last_end_time != 0) {
		REFERENCE_TIME new_start_time = last_end_time + 1;
		REFERENCE_TIME new_end_time = new_start_time + (end_frame - start_frame);
		sample->SetTime((REFERENCE_TIME *)&new_start_time, (REFERENCE_TIME *)&new_end_time);
	} else {
		sample->SetTime((REFERENCE_TIME *)&start_frame, (REFERENCE_TIME *)&end_frame);
	}
#else
	sample->SetTime((REFERENCE_TIME *)&start_frame, (REFERENCE_TIME *)&end_frame);
#endif

	sample->SetSyncPoint(TRUE);

	bool isDiscontinuity = (queued_sample->IsDiscontinuity() == S_OK);
	sample->SetDiscontinuity(isDiscontinuity);

	queued_sample->Release();

	return true;
}

// FrameObserver from Device Filter
void DShowCapture::FrameReceived(IMediaSample * sample)
{
	debug("FrameReceived");
	media_sample_queue_.push(sample);
}
