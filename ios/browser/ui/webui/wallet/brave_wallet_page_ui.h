// Copyright (c) 2024 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_IOS_BROWSER_UI_WEBUI_WALLET_BRAVE_WALLET_PAGE_UI_H_
#define BRAVE_IOS_BROWSER_UI_WEBUI_WALLET_BRAVE_WALLET_PAGE_UI_H_

#include <memory>

#include "brave/components/brave_wallet/common/brave_wallet.mojom.h"
#include "brave/ios/browser/ui/webui/wallet/wallet_handler.h"
#include "brave/ios/browser/ui/webui/wallet/wallet_page_handler.h"
#include "ios/web/public/webui/web_ui_ios.h"
#include "ios/web/public/webui/web_ui_ios_controller.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"

class BraveWalletPageUI : public web::WebUIIOSController,
                          public brave_wallet::mojom::PageHandlerFactory {
 public:
  explicit BraveWalletPageUI(web::WebUIIOS* web_ui, const GURL& url);
  BraveWalletPageUI(const BraveWalletPageUI&) = delete;
  BraveWalletPageUI& operator=(const BraveWalletPageUI&) = delete;
  ~BraveWalletPageUI() override;

  // Instantiates the implementor of the mojom::PageHandlerFactory mojo
  // interface passing the pending receiver that will be internally bound.
  void BindInterface(
      mojo::PendingReceiver<brave_wallet::mojom::PageHandlerFactory> receiver);

 private:
  // brave_wallet::mojom::PageHandlerFactory:
  void CreatePageHandler(
      mojo::PendingReceiver<brave_wallet::mojom::PageHandler> page_receiver,
      mojo::PendingReceiver<brave_wallet::mojom::WalletHandler> wallet_receiver,
      mojo::PendingReceiver<brave_wallet::mojom::JsonRpcService>
          json_rpc_service,
      mojo::PendingReceiver<brave_wallet::mojom::BitcoinWalletService>
          bitcoin_rpc_service_receiver,
      mojo::PendingReceiver<brave_wallet::mojom::ZCashWalletService>
          zcash_service_receiver,
      mojo::PendingReceiver<brave_wallet::mojom::SwapService> swap_service,
      mojo::PendingReceiver<brave_wallet::mojom::AssetRatioService>
          asset_ratio_service,
      mojo::PendingReceiver<brave_wallet::mojom::KeyringService>
          keyring_service,
      mojo::PendingReceiver<brave_wallet::mojom::BlockchainRegistry>
          blockchain_registry,
      mojo::PendingReceiver<brave_wallet::mojom::TxService> tx_service,
      mojo::PendingReceiver<brave_wallet::mojom::EthTxManagerProxy>
          eth_tx_manager_proxy,
      mojo::PendingReceiver<brave_wallet::mojom::SolanaTxManagerProxy>
          solana_tx_manager_proxy,
      mojo::PendingReceiver<brave_wallet::mojom::FilTxManagerProxy>
          filecoin_tx_manager_proxy,
      mojo::PendingReceiver<brave_wallet::mojom::BraveWalletService>
          brave_wallet_service,
      mojo::PendingReceiver<brave_wallet::mojom::BraveWalletP3A>
          brave_wallet_p3a,
      mojo::PendingReceiver<brave_wallet::mojom::WalletPinService>
          brave_wallet_pin_service_receiver,
      mojo::PendingReceiver<brave_wallet::mojom::WalletAutoPinService>
          brave_wallet_auto_pin_service_receiver,
      mojo::PendingReceiver<brave_wallet::mojom::IpfsService>
          brave_wallet_ipfs_service_receiver,
      mojo::PendingReceiver<brave_wallet::mojom::MeldIntegrationService>
          meld_integration_service) override;

  std::unique_ptr<WalletPageHandler> page_handler_;
  std::unique_ptr<brave_wallet::WalletHandler> wallet_handler_;

  mojo::Receiver<brave_wallet::mojom::PageHandlerFactory>
      page_factory_receiver_{this};
};

#endif  // BRAVE_IOS_BROWSER_UI_WEBUI_WALLET_BRAVE_WALLET_PAGE_UI_H_
