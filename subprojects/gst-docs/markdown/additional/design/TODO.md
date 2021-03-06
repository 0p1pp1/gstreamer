# TODO - Future Development

## API/ABI

- implement return values from events in addition to the gboolean.
This should be done by making the event contain a `GstStructure` with
input/output values, similar to `GstQuery`. A typical use case is
performing a non-accurate seek to a keyframe, after the seek you
want to get the new stream time that will actually be used to update
the slider bar.

- make `_pad_push_event()` return a `GstFlowReturn`. Partly fixed with
  `GstPadEventFullFunction` since 1.8. Needs to be made generic.

- `GstEvent`, `GstMessage` register like `GstFormat` or `GstQuery`.

- query POSITION/DURATION return accuracy. Just a flag or accuracy
percentage.

- use | instead of + as divider in serialization of Flags
(gstvalue/gststructure)

- Elements in a bin have no clue about the final state of the parent
element since the bin sets the target state on its children in small
steps. This causes problems for elements that like to know the final
state (rtspsrc going to `PAUSED` or `READY` is different in that we can
avoid sending the useless `PAUSED` request).

- Make serialisation of structures more consistent, readable and nicer
code-wise.

## IMPLEMENTATION

  - implement more QOS, [qos](additional/design/qos.md).

  - implement BUFFERSIZE.

## DESIGN

  - unlinking pads in the `PAUSED` state needs to make sure the stream
    thread is not executing code. Can this be done with a flush to
    unlock all downstream chain functions? Do we do this automatically
    or let the app handle this?

# Fixed in 1.0

- Optimize negotiation. We currently do a `get_caps()` call when we
link pads, which could potentially generate a huge list of caps and
all their combinations, we need to avoid generating these huge lists
by generating them We also need to incrementally return
intersections etc, for this. somewhat incrementally when needed. We
can do this with a `gst_pad_iterate_caps()` call. We also need to
incrementally return intersections etc, for this. FIXED in 1.0 with
a filter on getcaps functions.

- rethink how we handle dynamic replugging wrt segments and other
events that already got pushed and need to be pushed again. Might
need `GstFlowReturn` from `gst_pad_push_event()`. FIXED in 1.0 with
sticky events.

- pad block has several issues (all Fixed in 1.0 with unified pad probes):

    - can???t block on selected things, like push, pull, `pad_alloc`,
    events, ???

    - can???t check why the block happened. We should also be able to
    get the item/ reason that blocked the pad.

    - it only blocks on datapassing. When EOS, the block never happens
    but ideally should because pad block should inform the app when
    there is no dataflow.

    - the same goes for segment seeks that don???t push in-band EOS
    events. Maybe segment seeks should also send an EOS event when
    they???re done.

    - blocking should only happen from one thread. If one thread does
    `pad_alloc` and another a push, the push might be busy while the
    block callback is done.

    - maybe this name is overloaded. We need to look at some more use
    cases before trying to fix this.

- rethink the way we do upstream renegotiation. Currently it???s done
with `pad_alloc` but this has many issues such as only being able to
suggest 1 format and the need to allocate a buffer of this suggested
format (some elements such as capsfilter only know about the format,
not the size). We would ideally like to let upstream renegotiate a
new format just like it did when it started. This could, for
example, easily be triggered with a RENEGOTIATE event. FIXED in 1.0
with RECONFIGURE events.

- Remove the result format value in queries. FIXED in 1.0

- Try to minimize the amount of acceptcaps calls when pushing buffers
around. The element pushing the buffer usually negotiated already
and decided on the format. The element receiving the buffer usually
has to accept the caps anyway. FIXED in 1.0, caps are no longer on
buffers.

