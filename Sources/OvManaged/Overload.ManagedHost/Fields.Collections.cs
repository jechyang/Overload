using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Xml;
using System.Reflection;
using System.Xml.Linq;

namespace Overload.ManagedHost;

public static unsafe partial class Bootstrap
{
    private const int MaxCollectionItems = 1024, MaxCollectionDepth = 8;
    private static Type? CollectionElement(Type type) => type.IsArray && type.GetArrayRank() == 1 ? type.GetElementType() :
        type.IsGenericType && type.GetGenericTypeDefinition() == typeof(List<>) ? type.GetGenericArguments()[0] : null;

    private static (uint Tag, uint Auxiliary) DescribeField(Type type, int depth = 0)
    {
        if (depth > MaxCollectionDepth) return (0, 0);
        if (type.IsEnum) {
            uint underlying = Tag(Enum.GetUnderlyingType(type));
            return (underlying % 2 == 0 ? 13u : 14u, underlying);
        }
        if (CollectionElement(type) is Type element) {
            if (depth >= MaxCollectionDepth) return (0, 0);
            var child = DescribeField(element, depth + 1);
            return child.Tag == 0 ? (0u, 0u) : (25u, child.Tag);
        }
        uint tag = Tag(type);
        if (tag != 0) return (tag, 0);
        if (type.IsDefined(typeof(SerializableAttribute), false) && !type.IsAbstract &&
            (type.IsValueType || type.GetConstructor(Type.EmptyTypes) != null)) {
            var fields = RecordFields(type);
            if (fields.Length > 0 && fields.All(f => DescribeField(f.FieldType, depth + 1).Tag != 0)) return (26u, 0);
        }
        return (0, 0);
    }
    private static FieldInfo[] RecordFields(Type type) => type.GetFields(BindingFlags.Public | BindingFlags.Instance)
        .Where(f => !f.IsInitOnly && !f.IsDefined(typeof(NonSerializedAttribute))).OrderBy(f => f.Name, StringComparer.Ordinal).ToArray();
    private static string WriteRecord(Type type, object? value, int depth) {
        Require(depth < MaxCollectionDepth, Status.InvalidArgument, "Record nesting exceeds limit");
        var root = new XElement("record", new XAttribute("schema", type.FullName!), new XAttribute("null", value == null ? "1" : "0"));
        object defaults = Activator.CreateInstance(type)!;
        foreach (var field in RecordFields(type)) {
            var node = CollectionItem("member", field.FieldType, field.GetValue(value ?? defaults), depth + 1);
            node.Add(new XAttribute("name", field.Name)); root.Add(node);
        }
        string result = root.ToString(SaveOptions.DisableFormatting);
        Require(Encoding.GetByteCount(result) <= MaxFieldTextBytes, Status.InvalidArgument, "Record exceeds byte limit");
        return result;
    }
    private static object? ReadRecord(Type type, string text, int depth) {
        Require(depth < MaxCollectionDepth && Encoding.GetByteCount(text) <= MaxFieldTextBytes, Status.InvalidArgument, "Record exceeds limits");
        using var reader = XmlReader.Create(new StringReader(text), new XmlReaderSettings { DtdProcessing = DtdProcessing.Prohibit, XmlResolver = null, MaxCharactersInDocument = MaxFieldTextBytes });
        var root = XElement.Load(reader);
        Require(root.Name == "record" && root.Attributes().Count() == 2 && (string?)root.Attribute("schema") == type.FullName && (string?)root.Attribute("null") is "0" or "1", Status.FieldTypeMismatch, "Record schema mismatch");
        var fields = RecordFields(type); var nodes = root.Elements().ToArray();
        Require(nodes.Length == fields.Length, Status.FieldTypeMismatch, "Record field count mismatch");
        object result = Activator.CreateInstance(type)!;
        for (int i = 0; i < fields.Length; ++i) {
            Require(nodes[i].Name == "member" && (string?)nodes[i].Attribute("name") == fields[i].Name, Status.FieldTypeMismatch, "Record field mismatch");
            var item = new XElement(nodes[i]); item.Attribute("name")!.Remove();
            fields[i].SetValue(result, ReadCollectionItem(item, fields[i].FieldType, depth + 1));
        }
        return (string?)root.Attribute("null") == "1" && !type.IsValueType ? null : result;
    }
    private static string CollectionSchema(Type type)
    {
        if (CollectionElement(type) is Type element)
            return (type.IsArray ? "array:" : "list:") + CollectionSchema(element);
        if (type.IsEnum) return "enum:" + type.Assembly.GetName().Name + ":" + type.FullName;
        if (UITag(type) == 24) return "component:" + ReferenceType(type);
        return type.FullName!;
    }
    private static object? DefaultCollectionItem(Type type)
    {
        if (CollectionElement(type) != null) return null;
        if (type == typeof(string)) return "";
        if (type.IsEnum) {
            var values = Enum.GetValues(type);
            Require(values.Length > 0, Status.UnsupportedField, "Cannot create an item of an empty enum");
            return values.GetValue(0);
        }
        return Activator.CreateInstance(type);
    }
    // Scalar bytes are the fixed 16-byte little-endian ABI union, never pointers.
    // Text is UTF-8 hex so embedded NUL and XML metacharacters roundtrip exactly.
    private static XElement CollectionItem(string name, Type type, object? item, int depth)
    {
        FieldValue value = PackField(type, item, out string text, depth);
        return new XElement(name,
            new XAttribute("tag", value.Tag), new XAttribute("auxiliary", value.Auxiliary),
            new XAttribute("scalar", Convert.ToHexString(new ReadOnlySpan<byte>(&value.Scalar, 16))),
            new XElement("text", Convert.ToHexString(Encoding.GetBytes(text))));
    }
    private static string WriteCollection(Type type, object? value, int depth)
    {
        Require(depth < MaxCollectionDepth, Status.InvalidArgument, "Collection nesting exceeds limit");
        Type element = CollectionElement(type)!;
        IList? items = value as IList;
        Require(value == null || items != null, Status.FieldTypeMismatch, "Expected array or List<T>");
        Require(items == null || items.Count <= MaxCollectionItems, Status.InvalidArgument, "Collection length exceeds limit");
        var root = new XElement("list", new XAttribute("schema", CollectionSchema(type)), new XAttribute("null", value == null ? "1" : "0"));
        root.Add(CollectionItem("default", element, DefaultCollectionItem(element), depth + 1));
        int characters = root.ToString(SaveOptions.DisableFormatting).Length;
        if (items != null) foreach (object? item in items) {
            var node = CollectionItem("item", element, item, depth + 1);
            characters = checked(characters + node.ToString(SaveOptions.DisableFormatting).Length);
            Require(characters <= MaxFieldTextBytes, Status.InvalidArgument, "Collection encoded size exceeds limit");
            root.Add(node);
        }
        string result = root.ToString(SaveOptions.DisableFormatting);
        Require(Encoding.GetByteCount(result) <= MaxFieldTextBytes, Status.InvalidArgument, "Collection encoded bytes exceed limit");
        return result;
    }
    private static object? ReadCollectionItem(XElement node, Type type, int depth)
    {
        Require(node.Attributes().Count() == 3 && node.Attribute("tag") != null && node.Attribute("auxiliary") != null && node.Attribute("scalar") != null,
            Status.InvalidArgument, "Invalid collection item metadata");
        Require(node.Elements().Count() == 1 && node.Element("text") is XElement textNode && !textNode.HasElements && !textNode.HasAttributes,
            Status.InvalidArgument, "Invalid collection item text");
        FieldValue value = new() { Size = 48 };
        Require(uint.TryParse((string?)node.Attribute("tag"), NumberStyles.None, CultureInfo.InvariantCulture, out value.Tag) &&
            uint.TryParse((string?)node.Attribute("auxiliary"), NumberStyles.None, CultureInfo.InvariantCulture, out value.Auxiliary),
            Status.InvalidArgument, "Invalid collection item tag");
        byte[] scalar, text;
        try {
            scalar = Convert.FromHexString((string)node.Attribute("scalar")!);
            text = Convert.FromHexString(node.Element("text")!.Value);
        } catch (FormatException) { throw new Failure(Status.InvalidArgument, "Invalid collection hex encoding"); }
        Require(scalar.Length == 16 && text.Length <= MaxFieldTextBytes, Status.InvalidArgument, "Invalid collection payload size");
        scalar.CopyTo(new Span<byte>(&value.Scalar, 16));
        fixed (byte* p = text) {
            value.Text = new Utf8 { Data = p, Length = (ulong)text.Length };
            return UnpackField(type, &value, depth);
        }
    }
    private static object? ReadCollection(Type type, string text, int depth)
    {
        Require(depth < MaxCollectionDepth && Encoding.GetByteCount(text) <= MaxFieldTextBytes,
            Status.InvalidArgument, "Collection size/depth exceeds limit");
        XElement root;
        try {
            using var reader = XmlReader.Create(new StringReader(text), new XmlReaderSettings {
                DtdProcessing = DtdProcessing.Prohibit, XmlResolver = null, MaxCharactersInDocument = MaxFieldTextBytes
            });
            root = XElement.Load(reader, LoadOptions.None);
        } catch (XmlException) { throw new Failure(Status.InvalidArgument, "Invalid collection XML"); }
        Require(root.Name == "list" && root.Attributes().Count() == 2 && (string?)root.Attribute("schema") == CollectionSchema(type) &&
            (string?)root.Attribute("null") is "0" or "1", Status.FieldTypeMismatch, "Collection element schema mismatch");
        Type element = CollectionElement(type)!;
        var nodes = root.Elements().ToArray();
        Require(nodes.Length is >= 1 and <= MaxCollectionItems + 1 && nodes[0].Name == "default" && nodes.Skip(1).All(n => n.Name == "item"),
            Status.InvalidArgument, "Invalid collection members or item count");
        _ = ReadCollectionItem(nodes[0], element, depth + 1);
        bool isNull = (string?)root.Attribute("null") == "1";
        Require(!isNull || nodes.Length == 1, Status.InvalidArgument, "Null collection cannot contain items");
        if (isNull) return null;
        // Validate all elements into fresh storage before changing the live field.
        IList items = type.IsArray ? Array.CreateInstance(element, nodes.Length - 1) : (IList)Activator.CreateInstance(type)!;
        for (int i = 1; i < nodes.Length; ++i) {
            object? item = ReadCollectionItem(nodes[i], element, depth + 1);
            if (type.IsArray) items[i - 1] = item; else items.Add(item);
        }
        return items;
    }
}
