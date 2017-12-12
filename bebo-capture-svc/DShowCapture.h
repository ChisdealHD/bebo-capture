#pragma once

#include "sink_filter_win.h"
#include "shared_queue.h"
#include <atomic>

class DShowCapture : public SinkFilterObserver {
public:
	DShowCapture();
	~DShowCapture();

	void QueryCapabilities();
	void EnumFilters();
	void AddDeviceFilter();
	void CreateFilterGraph();
	void AddDemuxFilter();
	void AddSinkFilter();
	void Run();

	bool Initialize();
	bool GetFrame(IMediaSample *sample);

	void FrameReceived(IMediaSample* sample) override;

private:
	IGraphBuilder *graph;
	ICaptureGraphBuilder2 *builder;
	IMediaControl *mediaControl;
	IBaseFilter *deviceFilter;

	SinkFilter *sinkFilter;
	IPin *sinkInputPin;
	shared_queue<IMediaSample*> media_sample_queue_;

	bool initialized;
};

