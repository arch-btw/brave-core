// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import Foundation
import Shared
import UIKit
import os.log

extension FileManager {
  /// URL where files downloaded by user are stored.
  /// If the download folder doesn't exists it creates a new one
  @available(
    *,
    deprecated,
    message: "Use AsyncFileManager.url(for:appending:create:excludeFromBackups:)"
  )
  public func downloadsPath() throws -> URL {
    FileManager.default.getOrCreateFolder(
      name: "Downloads",
      excludeFromBackups: true,
      location: .documentDirectory
    )

    return try FileManager.default.url(
      for: .documentDirectory,
      in: .userDomainMask,
      appropriateFor: nil,
      create: false
    ).appendingPathComponent("Downloads", isDirectory: true)
  }

  public enum Folder: String {
    case cookie
    case webSiteData

    public var rawValue: String {
      switch self {
      case .cookie: return "/Cookies"
      case .webSiteData:
        #if targetEnvironment(simulator)
        if let bundleId = Bundle.main.bundleIdentifier {
          return "/WebKit/\(bundleId)/WebsiteData"
        }
        #endif
        return "/WebKit/WebsiteData"
      }
    }
  }
  public typealias FolderLockObj = (folder: Folder, lock: Bool)

  // Lock a folder using FolderLockObj provided.
  @available(*, deprecated, message: "Use AsyncFileManager.setWebDataAccess(atPath:)")
  @discardableResult public func setFolderAccess(_ lockObjects: [FolderLockObj]) -> Bool {
    guard let baseDir = baseDirectory() else { return false }
    for lockObj in lockObjects {
      do {
        try self.setAttributes(
          [.posixPermissions: (lockObj.lock ? 0 : 0o755)],
          ofItemAtPath: baseDir + lockObj.folder.rawValue
        )
      } catch {
        Logger.module.error(
          "Failed to \(lockObj.lock ? "Lock" : "Unlock") item at path \(lockObj.folder.rawValue) with error: \n\(error.localizedDescription)"
        )
        return false
      }
    }
    return true
  }

  // Check the locked status of a folder. Returns true for locked.
  @available(*, deprecated, message: "Use AsyncFileManager.isWebDataLocked(atPath:)")
  public func checkLockedStatus(folder: Folder) -> Bool {
    guard let baseDir = baseDirectory() else { return false }
    do {
      if let lockValue = try self.attributesOfItem(atPath: baseDir + folder.rawValue)[
        .posixPermissions
      ] as? NSNumber {
        return lockValue == 0o755
      }
    } catch {
      Logger.module.error(
        "Failed to check lock status on item at path \(folder.rawValue) with error: \n\(error.localizedDescription)"
      )
    }
    return false
  }

  /// Creates a folder at given location and returns its URL.
  /// If folder already exists, returns its URL as well.
  @available(
    *,
    deprecated,
    message: "Use AsyncFileManager.url(for:appending:create:excludeFromBackups:)"
  )
  @discardableResult
  public func getOrCreateFolder(
    name: String,
    excludeFromBackups: Bool = true,
    location: SearchPathDirectory = .applicationSupportDirectory
  ) -> URL? {
    guard let documentsDir = location.url else { return nil }

    var folderDir = documentsDir.appendingPathComponent(name)

    if fileExists(atPath: folderDir.path) { return folderDir }

    do {
      try createDirectory(at: folderDir, withIntermediateDirectories: true, attributes: nil)

      if excludeFromBackups {
        var resourceValues = URLResourceValues()
        resourceValues.isExcludedFromBackup = true
        try folderDir.setResourceValues(resourceValues)
      }

      return folderDir
    } catch {
      Logger.module.error("Failed to create folder, error: \(error.localizedDescription)")
      return nil
    }
  }

  public func removeFolder(withName name: String, location: SearchPathDirectory) {
    guard let locationUrl = location.url else { return }
    let fileUrl = locationUrl.appendingPathComponent(name)

    if !fileExists(atPath: fileUrl.path) {
      Logger.module.debug("File \(fileUrl) doesn't exist")
      return
    }

    do {
      try removeItem(at: fileUrl)
    } catch {
      Logger.module.error("\(error.localizedDescription)")
    }
  }

  private func baseDirectory() -> String? {
    return NSSearchPathForDirectoriesInDomains(.libraryDirectory, .userDomainMask, true).first
  }

  /// Returns size of contents of the directory in bytes.
  /// Returns nil if any error happened during the size calculation.
  @available(*, deprecated, message: "Use AsyncFileManager.sizeOfDirectory(at:)")
  public func directorySize(at directoryURL: URL) throws -> UInt64? {
    let allocatedSizeResourceKeys: Set<URLResourceKey> = [
      .isRegularFileKey,
      .fileAllocatedSizeKey,
      .totalFileAllocatedSizeKey,
    ]

    func fileSize(_ file: URL) throws -> UInt64 {
      let resourceValues = try file.resourceValues(forKeys: allocatedSizeResourceKeys)

      // We only look at regular files.
      guard resourceValues.isRegularFile ?? false else {
        return 0
      }

      // To get the file's size we first try the most comprehensive value in terms of what
      // the file may use on disk. This includes metadata, compression (on file system
      // level) and block size.
      // In case totalFileAllocatedSize is unavailable we use the fallback value (excluding
      // meta data and compression) This value should always be available.
      return UInt64(resourceValues.totalFileAllocatedSize ?? resourceValues.fileAllocatedSize ?? 0)
    }

    // We have to enumerate all directory contents, including subdirectories.
    guard
      let enumerator = self.enumerator(
        at: directoryURL,
        includingPropertiesForKeys: Array(allocatedSizeResourceKeys)
      )
    else { return nil }

    var totalSize: UInt64 = 0

    // Perform the traversal.
    for item in enumerator {
      // Add up individual file sizes.
      guard let contentItemURL = item as? URL else { continue }
      totalSize += try fileSize(contentItemURL)
    }

    return totalSize
  }
}

extension FileManager.SearchPathDirectory {

  /// Returns first url in user domain mask of given search path directory
  @available(*, deprecated, message: "Delete when getOrCreateFolder is removed")
  fileprivate var url: URL? {
    return FileManager.default.urls(for: self, in: .userDomainMask).first
  }
}
