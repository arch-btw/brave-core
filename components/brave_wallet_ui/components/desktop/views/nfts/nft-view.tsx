// Copyright (c) 2022 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at https://mozilla.org/MPL/2.0/.

import * as React from 'react'
import { useSelector } from 'react-redux'

// types
import { SupportedTestNetworks, WalletState } from '../../../../constants/types'

// components
import { Nfts } from './components/nfts'
import { AllNetworksOption } from '../../../../options/network-filter-options'
import { NftIpfsBanner } from '../../nft-ipfs-banner/nft-ipfs-banner'
import { BannerWrapper } from '../../../shared/style'

export const NftView = () => {
  // redux
  const networkList = useSelector(
    ({ wallet }: { wallet: WalletState }) => wallet.networkList
  )
  const userVisibleTokensInfo = useSelector(
    ({ wallet }: { wallet: WalletState }) => wallet.userVisibleTokensInfo
  )
  const selectedNetworkFilter = useSelector(
    ({ wallet }: { wallet: WalletState }) => wallet.selectedNetworkFilter
  )

  // state
  const [showBanner, setShowBanner] = React.useState<boolean>(true)

  const nonFungibleTokens = React.useMemo(() => {
    if (selectedNetworkFilter.chainId === AllNetworksOption.chainId) {
      return userVisibleTokensInfo.filter(
        (token) =>
          !SupportedTestNetworks.includes(token.chainId) &&
          (token.isErc721 || token.isNft)
      )
    }

    return userVisibleTokensInfo.filter(
      (token) =>
        token.chainId === selectedNetworkFilter.chainId &&
        (token.isErc721 || token.isNft)
    )
  }, [userVisibleTokensInfo, selectedNetworkFilter])

  // methods
  const onDismissIpfsBanner = React.useCallback(() => {
    setShowBanner(false)
  }, [])

  return (
    <>
      {showBanner && (
        <BannerWrapper>
          <NftIpfsBanner onDismiss={onDismissIpfsBanner} />
        </BannerWrapper>
      )}
      <Nfts networks={networkList} nftList={nonFungibleTokens} />
    </>
  )
}
