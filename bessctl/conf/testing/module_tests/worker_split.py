import time


def test_workersplit():
    NUM_WORKERS = 2

    for i in range(NUM_WORKERS):
        bess.add_worker(wid=i, core=i)

    for wid in range(NUM_WORKERS):
        src = Source()
        ws = WorkerSplit()
        src -> ws

        for i in range(NUM_WORKERS):
            ws:i -> Sink()

        src.attach_task(wid=wid)

        bess.resume_all()
        time.sleep(0.1)
        bess.pause_all()

        # packets should flow onto only one output gate...
        ogates = bess.get_module_info(ws.name).ogates
        for ogate in ogates:
            if ogate.ogate == wid:
                assert ogate.pkts > 0
            else:
                assert ogate.pkts == 0

        bess.reset_modules()

CUSTOM_TEST_FUNCTIONS.append(test_workersplit)
