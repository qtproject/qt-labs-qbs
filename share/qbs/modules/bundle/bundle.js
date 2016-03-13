/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing
**
** This file is part of the Qt Build Suite.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms and
** conditions see http://www.qt.io/terms-conditions. For further information
** use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 or version 3 as published by the Free
** Software Foundation and appearing in the file LICENSE.LGPLv21 and
** LICENSE.LGPLv3 included in the packaging of this file.  Please review the
** following information to ensure the GNU Lesser General Public License
** requirements will be met: https://www.gnu.org/licenses/lgpl.html and
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, The Qt Company gives you certain additional
** rights.  These rights are described in The Qt Company LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
****************************************************************************/

var DarwinTools = loadExtension("qbs.DarwinTools");
var PropertyList = loadExtension("qbs.PropertyList");

// Order is significant due to productTypeIdentifier() search path
var _productTypeIdentifiers = {
    "inapppurchase": "com.apple.product-type.in-app-purchase-content",
    "applicationextension": "com.apple.product-type.app-extension",
    "xpcservice": "com.apple.product-type.xpc-service",
    "application": "com.apple.product-type.application",
    "dynamiclibrary": "com.apple.product-type.framework",
    "loadablemodule": "com.apple.product-type.bundle",
    "staticlibrary": "com.apple.product-type.framework.static",
    "kernelmodule": "com.apple.product-type.kernel-extension"
};

function productTypeIdentifier(productType) {
    for (var k in _productTypeIdentifiers) {
        if (productType.contains(k))
            return _productTypeIdentifiers[k];
    }
    return "com.apple.package-type.wrapper";
}

var XcodeBuildSpecsReader = (function () {
    function XcodeBuildSpecsReader(developerPath, additionalSettings, useShallowBundles) {
        this._additionalSettings = additionalSettings;
        this._useShallowBundles = useShallowBundles;
        var i;
        var plist = new PropertyList();
        var plist2 = new PropertyList();
        try {
            developerPath += "/Platforms/MacOSX.platform/Developer/Library/Xcode/Specifications";
            plist.readFromFile(developerPath + "/MacOSX Package Types.xcspec");
            plist2.readFromFile(developerPath + "/MacOSX Product Types.xcspec");
            this._packageTypes = plist.toObject();
            this._productTypes = plist2.toObject();
            this._types = {};
            for (i = 0; i < this._packageTypes.length; ++i)
                this._types[this._packageTypes[i]["Identifier"]] = this._packageTypes[i];
            for (i = 0; i < this._productTypes.length; ++i)
                this._types[this._productTypes[i]["Identifier"]] = this._productTypes[i];
        } finally {
            plist.clear();
            plist2.clear();
        }
    }
    XcodeBuildSpecsReader.prototype.settings = function (typeIdentifier, recursive, skipPackageTypes) {
        // Silently use shallow bundles when preferred since it seems to be some sort of automatic
        // shadowing mechanism. For example, this matches Xcode behavior where static frameworks
        // are shallow even though no such product specification exists, and also seems to match
        // other behavior i.e. where productType in pbxproj files is never explicitly shallow.
        if (this._useShallowBundles && this._types[typeIdentifier + ".shallow"] && !skipPackageTypes)
            typeIdentifier += ".shallow";

        var typesObject = this._types[typeIdentifier];
        if (typesObject) {
            var buildProperties = {};

            if (recursive) {
                // Get all the settings for the product's package type
                if (!skipPackageTypes && typesObject["PackageTypes"]) {
                    for (var k = 0; k < typesObject["PackageTypes"].length; ++k) {
                        var props = this.settings(typesObject["PackageTypes"][k], recursive, true);
                        for (var y in props) {
                            if (props.hasOwnProperty(y))
                                buildProperties[y] = props[y];
                        }
                        break;
                    }
                }

                // Get all the settings for the product's inherited product type
                if (typesObject["BasedOn"]) {
                    // We'll only do the auto shallow substitution for wrapper package types...
                    // this ensures that in-app purchase content bundles are non-shallow on both
                    // OS X and iOS, for example (which matches Xcode behavior)
                    var isWrapper = false;
                    if (typesObject["ProductReference"]) {
                        var fileType = typesObject["ProductReference"]["FileType"];
                        if (fileType)
                            isWrapper = fileType.startsWith("wrapper.");
                    }

                    // Prevent recursion loop if this spec's base plus .shallow would be the same
                    // as the current spec's identifier
                    var baseIdentifier = typesObject["BasedOn"];
                    if (this._useShallowBundles && isWrapper
                            && this._types[baseIdentifier + ".shallow"]
                            && typeIdentifier !== baseIdentifier + ".shallow")
                        baseIdentifier += ".shallow";

                    props = this.settings(baseIdentifier, recursive, true);
                    for (y in props) {
                        if (props.hasOwnProperty(y))
                            buildProperties[y] = props[y];
                    }
                }
            }


            if (typesObject["Type"] === "PackageType") {
                props = typesObject["DefaultBuildSettings"];
                for (y in props) {
                    if (props.hasOwnProperty(y))
                        buildProperties[y] = props[y];
                }
            }

            if (typesObject["Type"] === "ProductType") {
                props = typesObject["DefaultBuildProperties"];
                for (y in props) {
                    if (props.hasOwnProperty(y))
                        buildProperties[y] = props[y];
                }
            }

            return buildProperties;
        }
    };
    XcodeBuildSpecsReader.prototype.setting = function (typeIdentifier, settingName) {
        var obj = this.settings(typeIdentifier, false);
        if (obj) {
            return obj[settingName];
        }
    };
    XcodeBuildSpecsReader.prototype.expandedSettings = function (typeIdentifier) {
        var obj = this.settings(typeIdentifier, true);
        if (obj) {
            for (var k in obj)
                obj[k] = this.expandedSetting(typeIdentifier, k);
            return obj;
        }
    };
    XcodeBuildSpecsReader.prototype.expandedSetting = function (typeIdentifier, settingName) {
        var obj = this.settings(typeIdentifier, true);
        if (obj) {
            for (var x in this._additionalSettings) {
                var additionalSetting = this._additionalSettings[x];
                if (additionalSetting !== undefined)
                    obj[x] = additionalSetting;
            }
            var setting = obj[settingName];
            var original;
            while (original !== setting) {
                original = setting;
                setting = DarwinTools.expandPlistEnvironmentVariables({ key: setting }, obj, true)["key"];
            }
            return setting;
        }
    };
    return XcodeBuildSpecsReader;
}());
