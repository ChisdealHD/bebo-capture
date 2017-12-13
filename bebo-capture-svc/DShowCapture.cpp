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

#define RETURN_ON_FAILED(hr, message, ...) \
 { \
	info("%S:%d %d", __func__, __LINE__, hr);\
	if (FAILED(hr)) { \
		error(message,  ##__VA_ARGS__ ); \
		return; \
	} \
 }

DShowCapture::DShowCapture() :
	initialized_(false),
	sink_filter_(nullptr),
	sink_input_pin_(nullptr),
	graph_(nullptr),
	builder_(nullptr),
	media_control_(nullptr),
	device_filter_(nullptr),
	device_video_output_pin_(nullptr),
	device_audio_output_pin_(nullptr)
{
	sink_filter_ = new SinkFilter(this);
	sink_input_pin_ = sink_filter_->GetPin(0);
}

DShowCapture::~DShowCapture() {
	if (media_control_.Get()) {
		media_control_->Stop();
	}

	if (graph_) {
		if (device_filter_) {
			graph_->RemoveFilter(device_filter_.Get());
		}

		if (sink_filter_) {
			graph_->RemoveFilter(sink_filter_.Get());
		}
	}
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

void DShowCapture::CreateFilterGraph() {
	HRESULT hr;

	hr = CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER,
		IID_IFilterGraph, (void**)&graph_);
	RETURN_ON_FAILED(hr, "CoCreateInstance IID_IFilterGraph failed. hr: %d", hr);

	hr = CoCreateInstance(CLSID_CaptureGraphBuilder2, NULL,
		CLSCTX_INPROC_SERVER,
		IID_ICaptureGraphBuilder2, (void**)&builder_);
	RETURN_ON_FAILED(hr, "CoCreateInstance IID_ICaptureGraphBuilder2 failed. hr: %d", hr);

	hr = builder_->SetFiltergraph(graph_.Get());
	RETURN_ON_FAILED(hr, "Failed to setFilterGraph on GraphBuilder. hr: %d", hr);

	hr = graph_->QueryInterface(IID_IMediaControl, (void**)&media_control_);
	RETURN_ON_FAILED(hr, "Failed to QueryInterface IID_IMediaControl. hr: %d", hr);
}

void DShowCapture::AddDeviceFilter(GUID device_guid) {
	HRESULT hr = CoCreateInstance(device_guid, NULL, CLSCTX_INPROC, IID_IBaseFilter, (void **)&device_filter_);
	RETURN_ON_FAILED(hr, "Failed to create device filter. hr: %d", hr);

	hr = graph_->AddFilter(device_filter_.Get(), NULL);
	RETURN_ON_FAILED(hr, "Failed to add device filter. hr: %d", hr);

	hr = graph_->AddFilter(sink_filter_.Get(), NULL);
	RETURN_ON_FAILED(hr, "Failed to add sink filter. hr: %d", hr);

	hr = builder_->FindPin(device_filter_.Get(), PINDIR_OUTPUT, &PIN_CATEGORY_CAPTURE, &MEDIATYPE_Video, TRUE, 0, &device_video_output_pin_);
	RETURN_ON_FAILED(hr, "Failed to find video capture output pin. hr: %d", hr);

	ComPtr<IAMStreamConfig> stream_config;
	hr = device_video_output_pin_->QueryInterface(stream_config.GetAddressOf());
	RETURN_ON_FAILED(hr, "Failed to query IAMStreamConfig interface. hr: %d", hr);

	int count = 0, size = 0;
	hr = stream_config->GetNumberOfCapabilities(&count, &size);
	RETURN_ON_FAILED(hr, "Failed to get number of capabilities. hr: %d", hr);

	std::unique_ptr<BYTE[]> caps(new BYTE[size]{ 0 });

	int capability_index = 1;
	for (int i = 0; i < count; i++) {
		AM_MEDIA_TYPE *media_type;
		HRESULT hr = stream_config->GetStreamCaps(i, &media_type, caps.get());
		if (hr != S_OK) {
			error("HR:%d, GetStreamCaps != S_OK", hr);
			continue;
		}

		pmt_log::debug_pmt("GetStreamCaps", media_type);
		DeleteMediaType(media_type);
	}

	// TODO: Make a function to find the best match capability
	AM_MEDIA_TYPE *format;
	hr = stream_config->GetStreamCaps(capability_index, &format, caps.get());
	RETURN_ON_FAILED(hr, "Failed to get specific device caps. index: %d, hr: %d", capability_index, hr);

#if 0
	if (format->formattype == FORMAT_VideoInfo) {
		VIDEOINFOHEADER* h =
			reinterpret_cast<VIDEOINFOHEADER*>(format->pbFormat);
		h->AvgTimePerFrame = UNITS / 30;
	}
#endif

	pmt_log::debug_pmt("setting device format", format);

	hr = stream_config->SetFormat(format); // TODO: This sometimes return S_FALSE
	RETURN_ON_FAILED(hr, "Failed to set device format. hr: %d", hr);

#if 0

	ComPtr<IAMBufferNegotiation> buffer_negotiation;
	hr = device_output_pin_->QueryInterface(buffer_negotiation.GetAddressOf());
	RETURN_ON_FAILED(hr, "Failed to query IAMBufferNegotiation interface. hr: %d", hr);
	ALLOCATOR_PROPERTIES allocator_properties;
	memset(&allocator_properties, 0, sizeof(allocator_properties));
	buffer_negotiation->GetAllocatorProperties(&allocator_properties);
	info("current allocator Properties count:%d", allocator_properties.cBuffers);

	if (allocator_properties.cBuffers < 2) {
		allocator_properties.cBuffers = 5;
		hr = buffer_negotiation->SuggestAllocatorProperties(&allocator_properties);
		if (hr != S_OK) {
			warn("SuggestAllocatorProperties returned %d", hr);
		}
	}
#endif

	DeleteMediaType(format);
	info("Finished adding device filter. HR: %d", hr);

	hr = graph_->ConnectDirect(device_video_output_pin_.Get(), sink_input_pin_.Get(), NULL);
	RETURN_ON_FAILED(hr, "Failed to directly connect device output pin to sink input pin. hr: %d", hr);

	info("Finished adding sink filter. HR: %d", hr);
}

void DShowCapture::AddDeviceAudioRendererFilter()
{
	HRESULT hr;

	hr = CoCreateInstance(CLSID_DSoundRender, NULL, CLSCTX_INPROC, 
		IID_IBaseFilter, (void**) &audio_renderer_filter_);
	RETURN_ON_FAILED(hr, "Failed CoCreateInstance DirectSound Renderer. hr: %d", hr);

	hr = graph_->AddFilter(audio_renderer_filter_.Get(), NULL);
	RETURN_ON_FAILED(hr, "Failed to add DirectSound audio renderer filter. hr: %d", hr);

	hr = builder_->FindPin(device_filter_.Get(), PINDIR_OUTPUT, &PIN_CATEGORY_CAPTURE, &MEDIATYPE_Audio, TRUE, 0, &device_audio_output_pin_);
	RETURN_ON_FAILED(hr, "Failed to find audio output pin. hr: %d", hr);

	// TODO: if elgato
	ComPtr<IAMClockSlave> clock_slave;
	hr = audio_renderer_filter_->QueryInterface(IID_IAMClockSlave, (void**)&clock_slave);
	RETURN_ON_FAILED(hr, "Failed to query interface clock slave. hr: %d", hr);

	hr = clock_slave->SetErrorTolerance(200);
	RETURN_ON_FAILED(hr, "Failed to set error tolerance. hr: %d", hr);

	hr = builder_->RenderStream(NULL, &MEDIATYPE_Audio, device_audio_output_pin_.Get(), NULL, audio_renderer_filter_.Get());
	RETURN_ON_FAILED(hr, "Failed to render audio stream. hr: %d", hr);
}

bool DShowCapture::Initialize() {
	if (initialized_) return true;

	initialized_ = true;
	CreateFilterGraph();
	AddDeviceFilter(CLSID_ElgatoVideoCaptureFilter);
	AddDeviceAudioRendererFilter();
	Run();
	return true;
}

void DShowCapture::Run() {
	info("DShowCapture::Run()");
	HRESULT hr = media_control_->Pause();
	RETURN_ON_FAILED(hr, "Failed to pause media control before running. hr: %d", hr);

	hr = media_control_->Run();
	info("DShowCapture::Run() hr: %d", hr);
}

// FillBuffer - Blocking
bool DShowCapture::GetFrame(IMediaSample ** left_hand_sample)
{
	debug("GetFrame - start - queue size %d", media_sample_queue_.size());

	IMediaSample *queued_sample;
	long ms = 1000;
	while (!media_sample_queue_.wait_for_and_pop(queued_sample, ms)) {
		debug("No sample within %d ms", ms);
		return false;
	}
	debug("GetFrame - got frame from queue");
	*left_hand_sample = queued_sample;
	return true;
}

// FrameObserver from Device Filter
void DShowCapture::FrameReceived(IMediaSample * sample)
{
	debug("FrameReceived");
	media_sample_queue_.push(sample);
}