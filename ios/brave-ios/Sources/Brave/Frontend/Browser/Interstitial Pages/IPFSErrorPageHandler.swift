// Copyright 2023 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import BraveShared
import Foundation
import Shared

class IPFSErrorPageHandler: InterstitialPageHandler {

  static var disabledError: NSError {
    NSError(domain: "ERR_IPFS_DISABLED", code: 4000)
  }

  static var privateModeError: NSError {
    NSError(domain: "ERR_INCOGNITO_IPFS_NOT_ALLOWED", code: 5000)
  }

  func canHandle(error: NSError) -> Bool {
    (error.code == IPFSErrorPageHandler.disabledError.code
      && error.domain == IPFSErrorPageHandler.disabledError.domain)
      || (error.code == IPFSErrorPageHandler.privateModeError.code
        && error.domain == IPFSErrorPageHandler.privateModeError.domain)
  }

  func response(for model: ErrorPageModel) async -> (URLResponse, Data)? {
    guard let asset = Bundle.module.url(forResource: "GenericError", withExtension: "html") else {
      assert(false)
      return nil
    }

    guard var html = await AsyncFileManager.default.utf8Contents(at: asset) else {
      assert(false)
      return nil
    }

    let variables = [
      "page_title": model.originalURL.absoluteDisplayString,
      "error_code": "\(model.errorCode)",
      "error_title": Strings.Wallet.ipfsErrorTitle,
      "error_description": model.description + "<br><br>\(Strings.errorPageCantBeReachedTry)",
      "error_domain": model.domain,
    ]

    variables.forEach { (arg, value) in
      html = html.replacingOccurrences(of: "%\(arg)%", with: value)
    }

    let data = Data(html.utf8)
    let response = InternalSchemeHandler.response(forUrl: model.originalURL)
    return (response, data)
  }
}
