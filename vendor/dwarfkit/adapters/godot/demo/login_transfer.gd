# Sample scene script (BLUEPRINT.md Phase 7): configure the kit for Jungle 4,
# log in with Anchor (the QR prompt arrives through DkQrUI below) and send a
# small transfer.
extends Control

const CHAIN_ID := "73e4385a2708e6d7048834fbc1079f2fabb17b3c125b146af438971e90716c4d"
const API_URL := "https://jungle4.greymass.com"

var kit := DkSessionKit.new()
var ui := DkQrUI.new()
var session: DkSession


class DkQrUI:
    extends DkUserInterface

    signal prompt_shown(args: Dictionary)

    func _prompt(args: Dictionary) -> void:
        # args.elements holds {type: "qr", data: "esr:..."} for the login QR;
        # render it with any QR node, or print the URI.
        for element in args.get("elements", []):
            if element.get("type") == "qr":
                print("Scan with Anchor: ", element.get("data"))
            elif element.get("type") == "link":
                print(element.get("label", ""), ": ", element.get("data", {}).get("href", ""))
        prompt_shown.emit(args)

    func _status(message: String) -> void:
        print("[status] ", message)

    func _error(message: String) -> void:
        push_error(message)


func _ready() -> void:
    kit.set_ui(ui)
    kit.configure("dwarfkit-demo", CHAIN_ID, API_URL)
    kit.login_completed.connect(_on_logged_in)
    kit.error.connect(func(message: String): push_error(message))
    kit.login("", "")


func _on_logged_in(new_session: DkSession) -> void:
    session = new_session
    print("Logged in as %s@%s" % [session.get_actor(), session.get_permission()])
    session.transact_completed.connect(_on_transacted)
    session.error.connect(func(message: String): push_error(message))
    session.transact({
        "account": "eosio.token",
        "name": "transfer",
        "authorization": [{"actor": session.get_actor(), "permission": session.get_permission()}],
        "data": {
            "from": session.get_actor(),
            "to": "teamgreymass",
            "quantity": "0.0001 EOS",
            "memo": "sent with dwarfkit",
        },
    })


func _on_transacted(result: Dictionary) -> void:
    print("Transaction broadcast: ", result.get("transaction_id", "(not broadcast)"))
