// Port of wallet-plugin-cloudwallet src/utils.ts. The popup close listener
// and MessageEvent validation live in the embedder's WebViewBridge.
#pragma once

#include <dwarfkit/plugins/wallet/cloudwallet/types.hpp>

namespace dwarfkit::cloudwallet {

// Ensure a wallet-modified transaction only adds permitted actions: WAX fee
// transfers to txfee.wam and RAM purchases delivered to the original actor.
Result<void> validateModifications(const Transaction& original, const Transaction& modified);

}  // namespace dwarfkit::cloudwallet
