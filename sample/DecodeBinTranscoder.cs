//
// DecodeBinTranscoder.cs: sample transcoder using DecodeBin binding
//
// Authors:
//   Aaron Bockover (abockover@novell.com)
//
// (C) 2006 Novell, Inc.
//

using System;
using Gst;

public delegate void ErrorHandler(object o, ErrorArgs args);
public delegate void ProgressHandler(object o, ProgressArgs args);

public class ErrorArgs : EventArgs 
{
    public string Error;
}

public class ProgressArgs : EventArgs
{
    public long Duration;
    public long Position;
}

public class DecodeBinTranscoder : IDisposable
{
    private Pipeline pipeline;
    private Element filesrc;
    private Element filesink;
    private Element audioconvert;
    private Element encoder;
    private DecodeBin decodebin;
    
    private uint progress_timeout;
    
    public event EventHandler Finished;
    public event ErrorHandler Error;
    public event ProgressHandler Progress;
    
    public DecodeBinTranscoder()
    {
        ConstructPipeline();
    }
    
    public void Transcode(string inputFile, string outputFile)
    {
        filesrc.SetProperty("location", inputFile);
        filesink.SetProperty("location", outputFile);
        
        pipeline.SetState(State.Playing);
        progress_timeout = GLib.Timeout.Add(250, OnProgressTimeout);
    }
    
    public void Dispose()
    {
        pipeline.Dispose();
    }
    
    protected virtual void OnFinished()
    {
        EventHandler handler = Finished;
        if(handler != null) {
            handler(this, new EventArgs());
        }
    }
        
    protected virtual void OnError(string error)
    {
        ErrorHandler handler = Error;
        if(handler != null) {
            ErrorArgs args = new ErrorArgs();
            args.Error = error;
            handler(this, args);
        }
    }
    
    protected virtual void OnProgress(long position, long duration)
    {
        ProgressHandler handler = Progress;
        if(handler != null) {
            ProgressArgs args = new ProgressArgs();
            args.Position = position;
            args.Duration = duration;
            handler(this, args);
        }
    }

    private void ConstructPipeline()
    {
        pipeline = new Pipeline("pipeline");
        
        filesrc = ElementFactory.Make("filesrc", "filesrc");
        filesink = ElementFactory.Make("filesink", "filesink");
        audioconvert = ElementFactory.Make("audioconvert", "audioconvert");
        encoder = ElementFactory.Make("wavenc", "wavenc");
        decodebin = ElementFactory.Make("decodebin", "decodebin") as DecodeBin;
        decodebin.NewDecodedPad += OnNewDecodedPad;
        
        pipeline.AddMany(filesrc, decodebin, audioconvert, encoder, filesink);
        
        filesrc.Link(decodebin);
        audioconvert.Link(encoder);
        encoder.Link(filesink);
        
        pipeline.Bus.AddWatch(new BusFunc(OnBusMessage));
    }
    
    private void OnNewDecodedPad(object o, NewDecodedPadArgs args)
    {
        Pad sinkpad = audioconvert.GetPad("sink");
        
        if(sinkpad.IsLinked) {
            return;
        }
        
        Caps caps = args.Pad.Caps;
        Structure structure = caps.GetStructure(0);
        
        if(!structure.Name.StartsWith("audio")) {
            return;
        }
        
        args.Pad.Link(sinkpad);
    }
    
    private bool OnBusMessage(Bus bus, Message message)
    {
        switch(message.Type) {
            case MessageType.Error:
                string error;
                message.ParseError(out error);
                GLib.Source.Remove(progress_timeout);
                OnError(error);
                break;
            case MessageType.Eos:
                pipeline.SetState(State.Null);
                GLib.Source.Remove(progress_timeout);
                OnFinished();
                break;
        }

        return true;
    }
    
    private bool OnProgressTimeout()
    {
        long duration, position;
        
        if(pipeline.QueryDuration(Gst.Format.Time, out duration) &&
            encoder.QueryPosition(Gst.Format.Time, out position)) {
            OnProgress(position, duration);
        }
        
        return true;
    }
    
    private static GLib.MainLoop loop;
    
    public static void Main(string [] args)
    {
        if(args.Length < 2) {
            Console.WriteLine("Usage: mono decodebin-transcoder.exe <input-file> <output-file>");
            return;
        }
    
        Gst.Application.Init();
        loop = new GLib.MainLoop();
    
        DecodeBinTranscoder transcoder = new DecodeBinTranscoder();
        
        transcoder.Error += delegate(object o, ErrorArgs args) {
            Console.WriteLine("Error: {0}", args.Error);
            transcoder.Dispose();
            loop.Quit();
        };
        
        transcoder.Finished += delegate {
            Console.WriteLine("\nFinished");
            transcoder.Dispose();
            loop.Quit();
        };
        
        transcoder.Progress += delegate(object o, ProgressArgs args) {
            Console.Write("\rEncoding: {0} / {1} ({2:00.00}%) ", 
                new TimeSpan((args.Position / (long) Clock.GstSecond) * TimeSpan.TicksPerSecond), 
                new TimeSpan((args.Duration / (long) Clock.GstSecond) * TimeSpan.TicksPerSecond),
                ((double)args.Position / (double)args.Duration) * 100.0);
        };
        
        transcoder.Transcode(args[0], args[1]);
        
        loop.Run();
    }
}
