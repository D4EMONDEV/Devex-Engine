using System.Globalization;
using System.Text;

namespace Devex;

/// <summary>
/// The translations of the game, from the .csv tables of the project, and the language it shows.
/// The texts of the interface translate themselves; <see cref="Tr(string)"/> gives the others.
/// </summary>
public static unsafe class Localization
{
    /// <summary>
    /// The language shown, a code such as "fr" or "pt_BR". Setting it shows the closest language the
    /// tables have, and keeps it as the player's choice for the next launches.
    /// </summary>
    public static string Language
    {
        get => Utf8.ToString(Bootstrap.Native.Language()) ?? string.Empty;
        set
        {
            using var text = new Utf8Buffer(value);
            Bootstrap.Native.SetLanguage(text.Pointer);
        }
    }

    /// <summary>The languages the tables have a column for, sorted: what a menu of languages lists.</summary>
    public static string[] Languages
    {
        get
        {
            var languages = new string[Bootstrap.Native.LanguageCount()];
            for (int index = 0; index < languages.Length; ++index)
            {
                languages[index] = Utf8.ToString(Bootstrap.Native.LanguageAt(index)) ?? string.Empty;
            }
            return languages;
        }
    }

    /// <summary>What shows where the language has no message, as Project Settings set it.</summary>
    public static string FallbackLanguage => Utf8.ToString(Bootstrap.Native.FallbackLanguage()) ?? "en";

    /// <summary>The message of a key in the language shown, or the key itself when no table has one.</summary>
    public static string Tr(string key)
    {
        using var text = new Utf8Buffer(key);
        return Utf8.ToString(Bootstrap.Native.Translate(text.Pointer)) ?? key;
    }

    /// <summary>
    /// The same, with each {name} replaced by its value: <c>Tr("SCORE", ("points", 12))</c>. {{ and }}
    /// write braces; a name without a value stays as it is written.
    /// </summary>
    public static string Tr(string key, params (string Name, object? Value)[] values)
    {
        string message = Tr(key);
        var result = new StringBuilder(message.Length);
        for (int index = 0; index < message.Length; ++index)
        {
            char character = message[index];
            if ((character == '{' || character == '}') && index + 1 < message.Length && message[index + 1] == character)
            {
                result.Append(character);
                ++index;
                continue;
            }
            if (character == '{')
            {
                int end = message.IndexOf('}', index + 1);
                if (end > index)
                {
                    string name = message.Substring(index + 1, end - index - 1);
                    int found = Array.FindIndex(values, value => value.Name == name);
                    if (found >= 0)
                    {
                        result.Append(Convert.ToString(values[found].Value, CultureInfo.InvariantCulture));
                        index = end;
                        continue;
                    }
                }
            }
            result.Append(character);
        }
        return result.ToString();
    }

    /// <summary>The name of a language in itself, as a menu of languages shows it: "Français" for "fr".</summary>
    public static string LanguageName(string code)
    {
        using var text = new Utf8Buffer(code);
        return Utf8.ToString(Bootstrap.Native.LanguageName(text.Pointer)) ?? code;
    }
}
