// Copyright (c) 2024 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

import { BraveWallet } from '../constants/types'

export const getDappNetworkIds = (
  dappChains: string[],
  networks: BraveWallet.NetworkInfo[]
) => {
  const dappNetworks = []

  for (const chainName of dappChains) {
    const net = networks.find((n) =>
      n.chainName.toLowerCase().startsWith(
        // The dapps list uses '-' to separate words
        chainName.replaceAll('-', ' ')
      )
    )
    if (net) {
      dappNetworks.push(net.chainId)
    }
  }

  return dappNetworks
}

export function isDappMapEmpty<T, U>(map: Map<T, U[]>): boolean {
  // Check if the map has no categories
  if (map.size === 0) {
    return true
  }

  // Check if all categories have no items
  for (const [, items] of map) {
    if (items.length > 0) {
      return false
    }
  }

  return true
}
