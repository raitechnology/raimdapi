# Rai API Go binding

Package `raiapi2` (this module) mirrors the Java binding in `../java` and the
.NET binding in `../dotnet` one to one, so the Java documentation and programs
apply.  Like .NET it is a thin layer over the flat C api, so the two bindings
have the same shape:

```
 Go  raiapi2  --cgo-->  libraimdapi.so  --C++-->  libomm, libsassrv, libraimd, ...
```

`libraimdapi` (`include/raiapi2_c.h`, `src/raiapi/c/raiapi2_c.cpp`) is the flat
C api over the C++ classes: opaque handles, every call returns a `rai_err_t`
(NULL = ok), callbacks are function pointers with a closure.

| Java package           | Go                                                                                              |
|------------------------|-------------------------------------------------------------------------------------------------|
| `com.rai.raiapi2`      | `Api`, `Session`, `Queue`, `Subscribe`, `Publish`, `InteractivePublish`, `Timer`, `Dict`, `Entitlement`, `Args`, `StringArg`/`BoolArg`/`IntArg`/`DoubleArg`, the `Time` functions, `TimeRotate`, the `*Event` structs and the `*Callback` interfaces |
| `com.rai.raimsg`       | `Msg`, `Field`, `Partial`, the `SASS` constants (`INITIAL`, `STATUS_OK`, ...), `Sass*ToString` |
| `com.rai.raiexception` | `Error` (`Module`, `Errno`, `Reason`, `IsMsgError()`), `IsNotFound(err)`                        |

Method names are the Java ones in Go casing (`CreateSession`, `GetString`,
`AppendUShort`, ...).  Differences forced by the language:

- Errors are returned, not thrown: every call that can fail returns `error`
  (a `*raiapi2.Error` when it came from the api).  `Args` has `String` /
  `Bool` / `Int` / `Double` shorthands without the error for the common case
  where the arg name is known to exist.
- Callbacks are interfaces (`MsgCallback.OnMsg`, `TimerCallback.OnTimer`,
  `SubscribeCallback.OnSubscribe`, `DataLossCallback.OnDataLoss/OnConnection`)
  with an `any` closure.  A `*Msg` handed to a callback is owned by the api and
  only valid during the callback (as in Java); the binding invalidates it on
  return.
- No overloading: `NewSASSMsg` / `NewSASSMsgForm`, `PublishMsg` / `PublishBuf`,
  `Print` / `PrintFmt`, `OpenLog` / `OpenLogArgs`.  `Args.Add(arg, flags...)`
  takes any of the four arg types.
- `Msg.Print` / `PrintHex` / `PrintXML` take an `io.Writer`; `Args.AddDefaults`
  takes an `io.Writer` for the help/version output.
- Optional strings are `""` where Java uses `null` (`StringArg` default,
  publish prefix, dictionary subject).
- `RegisterSigHandler(func(int))` is implemented with `os/signal`, so the
  handler runs on a goroutine rather than in signal context.
- Native handles are released explicitly (`Api.Delete`, `Msg.Delete`,
  `Field.Delete`, `Args.Delete`); there are no finalizers.
- `Time.currentTimeMillis()` is `raiapi2.CurrentTimeMillis()`; the other
  `Time` statics are package functions with the same names.

## Building

```
make golang=1          # builds libraimdapi and g{raisub2,raipub2,raiping2,raireplay2}
make golang=1 golang_vet
```

Outputs: `FC43_x86_64/bin/g<prog>` (like the Java `j*` and .NET `d*`), ELF
binaries with an rpath to `FC43_x86_64/lib64` (removed by `dist_bins`, like
the C++ programs).  The version reported by `raiapi2.BindingVersion()` is
stamped from the same makefile variables as `RaiVersion()`.

`go build` also works directly; the header is found relative to the source,
the library needs the linker / loader path:

```
cd src/raiapi/golang
top=$(cd ../../.. && pwd); lib=$top/FC43_x86_64/lib64
CGO_LDFLAGS="-L$lib -Wl,-rpath,$lib" go build -o /tmp/ ./cmd/...
```

The rpm builds it with `rpmbuild --with golang` (needs `golang` in the
chroot).  Windows (`dist_win`) does not build the Go programs; it would need
a mingw cgo toolchain.

## Programs

Ports of the Java programs, same arguments, same output (`-help` output is
byte-identical to the .NET programs):

```
graisub2   -subject TEST.REC.AAA.NaE [-snap] [-listen] [-save file] [-rate] ...
graipub2   -subject TEST.REC.AAA.NaE -data "ASK=11.0,BID=10.5" -count 3
graiping2  -subject PING.TEST -perSec 200 -msgCount 1000
graireplay2 -fileName saved.replay -perSec 10 [-realtime 1.0]
```

`-help` lists everything; `-api <name>` selects the transport as in Java.

## Using the package

```go
import rai "github.com/injinj/raimdapi/src/raiapi/golang/raiapi2"

type sub struct{ api *rai.Api }

func (s *sub) OnMsg(ev *rai.MsgEvent, m *rai.Msg, _ any) {
	fmt.Printf("## %s (%s)\n%s", ev.SubscribedSubject(), rai.StateToString(ev.State), m)
}

func main() {
	api, err := rai.RaiOpen("", os.Args[1:])   // -api / -daemon / ... from argv
	...
	args, _ := rai.NewArgs()
	api.GetArgs(args)
	args.AddDefaults(rai.RaiVersion(), "rai_", os.Stderr, "mysub")
	if ok, _ := args.ProcessArgs(os.Args[1:]); !ok { return } // -help / -version
	api.ParseArgs(args)

	sess, _ := api.CreateSession()
	sess.Start()
	q, _ := sess.CreateQueue(false)
	sb, _ := q.CreateSubscribe(&sub{api}, nil)
	sb.Start("TEST.REC.AAA.NaE", rai.SUB_BOTH|rai.SUB_NO_COPY, 6000)
	for { q.TimedDispatch(100) }
}
```
