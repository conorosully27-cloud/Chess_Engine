// Talks to one engine worker. One job runs at a time: starting a new job, or
// calling stop(), ends the current one. A search can't be interrupted from
// outside while it runs, so stopping ends the worker itself; a fresh one
// starts with the next job. Between jobs the worker is kept, so its memory
// of positions carries over from move to move.

export class EngineClient {
  constructor() {
    this.worker = null;
    this.job = null; // { id, onInfo, onDone }
    this.nextId = 1;
  }

  get busy() {
    return this.job !== null;
  }

  // request: { type: 'analyse' | 'play', history, timeMs, depth }
  run(request, { onInfo, onDone, onError } = {}) {
    this.stop();
    const id = this.nextId++;
    this.job = { id, onInfo, onDone, onError };
    try {
      this.worker ??= this.spawn();
      this.worker.postMessage({ ...request, id });
    } catch (error) {
      this.job = null;
      onError?.(error);
    }
  }

  stop() {
    if (!this.job) return;
    this.job = null;
    this.worker?.terminate();
    this.worker = null;
  }

  spawn() {
    const worker = new Worker(new URL('./engine-worker.js', import.meta.url), { type: 'module' });
    worker.onmessage = ({ data }) => {
      const job = this.job;
      if (!job || data.id !== job.id) return;
      if (data.type === 'info') {
        job.onInfo?.(data.info);
      } else {
        this.job = null;
        job.onDone?.(data.result);
      }
    };
    worker.onerror = (event) => {
      const job = this.job;
      this.job = null;
      this.worker?.terminate();
      this.worker = null;
      job?.onError?.(event);
    };
    return worker;
  }
}
