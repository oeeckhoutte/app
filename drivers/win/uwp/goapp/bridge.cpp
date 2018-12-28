// goapp.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"

using namespace std;
using namespace concurrency;
using namespace Windows;
using namespace Windows::Foundation::Collections;
using namespace Windows::Data::Json;

AppServiceConnection ^ goapp = nullptr;

void Bridge_Init()
{
    auto conn = ref new AppServiceConnection();
    conn->AppServiceName = "InProcessAppService";
    conn->PackageFamilyName = ApplicationModel::Package::Current->Id->FamilyName;
    conn->RequestReceived +=
        ref new TypedEventHandler<AppServiceConnection ^,
                                  AppServiceRequestReceivedEventArgs ^>(
            BridgeRequestReceived);
    conn->ServiceClosed +=
        ref new TypedEventHandler<AppServiceConnection ^,
                                  AppServiceClosedEventArgs ^>(BridgeClosed);
    goapp = conn;

    create_task(conn->OpenAsync())
        .then([](AppServiceConnectionStatus status) {
            if (status != AppServiceConnectionStatus::Success)
            {
                cout << "bridge connection failed: " << (int)status << endl;
                return;
            }

			cout << "bridge init complete " << endl;

            Bridge_GoCall("driver.OnRun", "", "true");
        })
        .wait();

		cout << "bridge init return " << endl;
}

void BridgeRequestReceived(AppServiceConnection ^ connection,
                           AppServiceRequestReceivedEventArgs ^ args)
{
    auto deferral = args->GetDeferral();
    auto op = args->Request->Message->Lookup("Operation")->ToString();

    if (op == "Return")
    {
        auto returnID = args->Request->Message->Lookup("ReturnID")->ToString();
        auto input = args->Request->Message->Lookup("Input")->ToString();
        auto err = args->Request->Message->Lookup("Err")->ToString();

        Bridge_WinCallReturn(returnID, input, err);
        return;
    }

    auto method = args->Request->Message->Lookup("Method")->ToString();
    auto input = args->Request->Message->Lookup("Input")->ToString();
    auto ui = args->Request->Message->Lookup("Ui")->ToString();

    auto output = Bridge_GoCall(method, input, ui);
    auto msg = ref new ValueSet();

    if (output != nullptr)
    {
        msg->Insert("Value", output);
    }

    create_task(args->Request->SendResponseAsync(msg))
        .then([deferral](AppServiceResponseStatus status) {
            deferral->Complete();
        })
        .wait();
}

void BridgeClosed(AppServiceConnection ^ connection,
                  AppServiceClosedEventArgs ^ args)
{
    Bridge_GoCall("driver.OnClose", "", "true");
}

// ****************************************************************************
// *  UWP call                                                                *
// ****************************************************************************
void Bridge_Call(char *call)
{
    auto c = winString(call);
    auto cjson = JsonObject::Parse(c);
    auto returnID = cjson->GetNamedString("ReturnID");

    auto msg = ref new ValueSet();
    msg->Insert("Value", c);

    create_task(goapp->SendMessageAsync(msg))
        .then([returnID](AppServiceResponse ^ r) {
            if (r->Status != AppServiceResponseStatus::Success)
            {
                auto err = r->Status.ToString();
                Bridge_WinCallReturn(returnID, "", err);
            }
        })
        .wait();
}

// ****************************************************************************
// *  Win return                                                              *
// ****************************************************************************
funcWinReturn winReturn = NULL;

void Bridge_SetWinCallReturn(funcWinReturn f)
{
    winReturn = f;
}

void Bridge_WinCallReturn(String ^ retID, String ^ ret, String ^ err)
{
    char *cretID = cString(retID);
    char *cret = cString(ret);
    char *cerr = cString(err);
    winReturn(cretID, cret, cerr);

    free(cretID);
    free(cret);
    free(cerr);
}

// ****************************************************************************
// *  Go call                                                                 *
// ****************************************************************************
funcGoCall goCall = NULL;

void Bridge_SetGoCall(funcGoCall f)
{
    goCall = f;
}

String ^ Bridge_GoCall(String ^ method, String ^ input, String ^ ui) {
    JsonObject ^ call = ref new JsonObject();
    call->SetNamedValue("Method", JsonValue::CreateStringValue(method));

    if (input != nullptr && input->Length() != 0)
    {
        call->SetNamedValue("Input", JsonObject::Parse(input));
    }

    char *cinput = cString(call->ToString());
    char *cui = cString(ui->ToString());
    char *cret = goCall(cinput, cui);

    String ^ ret = winString(cret);

    free(cinput);
    free(cui);
    //free(cret);

    return ret;
}

// ****************************************************************************
// *  String convertions                                                      *
// ****************************************************************************
char *cString(String ^ s)
{
    if (s == nullptr || s->Length() == 0)
    {
        return NULL;
    }

    stdext::cvt::wstring_convert<std::codecvt_utf8<wchar_t>> convert;
    std::string stringUtf8 = convert.to_bytes(s->Data());

    char *cstr = (char *)stringUtf8.c_str();
    char *b = (char *)calloc(strlen(cstr), sizeof(char *));
    memmove(b, cstr, strlen(cstr));

    return b;
}

String ^ winString(char *str) {
    if (str == nullptr)
    {
        return nullptr;
    }

    string s_str(str);
    wstring wid_str = std::wstring(s_str.begin(), s_str.end());
    return ref new String(wid_str.c_str());
}
